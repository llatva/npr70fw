/**
  ******************************************************************************
  * @file    task_ethernet.c
  * @brief   Combined Ethernet RX/TX task - poll W5500 for RX and send from TX queue
  ******************************************************************************
  * @attention
  *
  * Combined task for Ethernet I/O: polls W5500 for incoming packets and sends
  * packets from the TX queue. Handles ARP, IPv4, and flow control.
  *
  ******************************************************************************
  */

#include "task_ethernet.h"
#include "app_common.h"
#include "w5500_driver.h"
#include "fec_codec.h"
#include <stdio.h>
#include <string.h>

/* Private defines -----------------------------------------------------------*/
#define ETHERTYPE_ARP       0x0806
#define ETHERTYPE_IPV4      0x0800
#define IP_PROTO_UDP        0x11
#define FDD_DOWN_PORT       0x1A3C  /* 6716 decimal */

#define TX_TIMEOUT_MS       100     /* Timeout for W5500 TX operation */
#define TX_RETRY_DELAY_MS   10      /* Delay between retries */
#define MAX_TX_RETRIES      3       /* Maximum transmission attempts */

/* Private variables ---------------------------------------------------------*/
static W5500_Context_t *hw5500 = NULL;

/* RX buffer - allocate from heap to save BSS space */
static uint8_t *rx_buffer = NULL;

/* Statistics */
static uint32_t rx_packet_count = 0;
static uint32_t arp_packet_count = 0;
static uint32_t ipv4_packet_count = 0;
static uint32_t tx_packet_count = 0;
static uint32_t tx_error_count = 0;

/* Private function prototypes -----------------------------------------------*/
static void ProcessARPPacket(uint8_t *data, uint16_t size);
static void ProcessIPv4Packet(uint8_t *data, uint16_t size);
static void InjectFDDDownlink(uint8_t *udp_payload, uint16_t payload_size);
static void RouteIPv4ToRadio(uint8_t *eth_frame, uint16_t size);
static uint32_t IP_CharToInt(const uint8_t *ip_bytes);
static uint8_t LookupClientIDFromIP(uint32_t ip_addr);
static void SegmentAndPush(const uint8_t *data_unsegmented, int total_size, uint8_t client_addr, uint8_t protocol);
static void IP_IntToChar(uint32_t ip_int, uint8_t *ip_char);

/**
 * @brief Initialize Ethernet task
 * @param w5500_ctx Pointer to W5500 driver context
 */
void EthernetTask_Init(W5500_Context_t *w5500_ctx)
{
    hw5500 = w5500_ctx;
    
    /* Allocate RX buffer from heap - size depends on SRAM availability */
    uint16_t rx_buf_size = GetActiveEthernetPacketDataSize();
    rx_buffer = (uint8_t *)pvPortMalloc(rx_buf_size);
    if (rx_buffer == NULL) {
        /* Allocation failed - critical error */
        printf("FATAL: Failed to allocate %u byte RX buffer for Ethernet task\r\n", rx_buf_size);
        while(1);  /* Trap */
    }
    printf("Ethernet task init: RX buffer = %u bytes\r\n", rx_buf_size);
}

/**
 * @brief Combined Ethernet RX/TX task
 * @param argument Not used
 */
void vEthernetTask(void *argument)
{
    uint16_t rx_size;
    uint16_t ethertype;
    HAL_StatusTypeDef status;
    EthernetPacket_t eth_packet;
    uint8_t retry_count;
    
    printf("Ethernet task started\r\n");
    
    /* Ensure buffer is allocated */
    if (rx_buffer == NULL) {
        printf("ERROR: RX buffer not allocated\r\n");
        vTaskDelete(NULL);
    }
    
    for (;;) {
        /* Handle RX: Check for received data on RAW socket */
        rx_size = W5500_GetRxSize(hw5500, W5500_SOCK_RAW);
        
        if (rx_size > 0) {
            /* Limit size to buffer capacity based on SRAM configuration */
            uint16_t max_rx_size = GetActiveEthernetPacketDataSize();
            if (rx_size > max_rx_size) {
                rx_size = max_rx_size;
            }
            
            /* Receive packet from W5500 */
            status = W5500_RecvData(hw5500, W5500_SOCK_RAW, rx_buffer, rx_size);
            
            if (status == HAL_OK && rx_size >= 16) {  /* Minimum Ethernet header + type */
                
                rx_packet_count++;
                
                /* Extract EtherType (bytes 14-15, after 14-byte Ethernet header) */
                ethertype = (rx_buffer[14] << 8) | rx_buffer[15];
                
                /* Process based on EtherType */
                switch (ethertype) {
                    case ETHERTYPE_ARP:
                        arp_packet_count++;
                        ProcessARPPacket(rx_buffer, rx_size);
                        break;
                        
                    case ETHERTYPE_IPV4:
                        ipv4_packet_count++;
                        ProcessIPv4Packet(rx_buffer, rx_size);
                        break;
                        
                    default:
                        /* Unknown EtherType - ignore */
                        break;
                }
            }
        }
        
        /* Handle TX: Check for packet in queue (non-blocking) */
        if (xQueueReceive(xEthernetTxQueue, &eth_packet, 0) == pdTRUE) {
            
            /* Validate packet against active buffer size */
            uint16_t max_packet_size = GetActiveEthernetPacketDataSize();
            if (eth_packet.length > 0 && eth_packet.length <= max_packet_size) {
                
                retry_count = 0;
                status = HAL_ERROR;
                
                /* Retry transmission on failure */
                while (retry_count < MAX_TX_RETRIES && status != HAL_OK) {
                    
                    /* Check if socket has free space */
                    uint16_t free_size = W5500_GetTxFreeSize(hw5500, eth_packet.socket);
                    
                    if (free_size >= eth_packet.length) {
                        /* Send packet via W5500 */
                        status = W5500_SendData(hw5500, eth_packet.socket, 
                                               eth_packet.data, eth_packet.length);
                        
                        if (status == HAL_OK) {
                            tx_packet_count++;
                        } else {
                            /* Transmission failed - retry */
                            retry_count++;
                            if (retry_count < MAX_TX_RETRIES) {
                                vTaskDelay(pdMS_TO_TICKS(TX_RETRY_DELAY_MS));
                            }
                        }
                    } else {
                        /* Socket buffer full - wait and retry */
                        retry_count++;
                        if (retry_count < MAX_TX_RETRIES) {
                            vTaskDelay(pdMS_TO_TICKS(TX_RETRY_DELAY_MS));
                        }
                    }
                }
                
                /* Check if all retries failed */
                if (status != HAL_OK) {
                    tx_error_count++;
                    /* TODO: Log error or notify higher layer */
                }
                
            } else {
                /* Invalid packet length */
                tx_error_count++;
            }
        }
        
        /* Small delay to prevent busy-waiting */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief Process ARP packet (proxy for radio clients)
 * @param data Pointer to Ethernet frame (starts with Ethernet header)
 * @param size Frame size
 * @note Reference: source/DHCP_ARP.cpp — ARP_process()
 * 
 * The modem acts as ARP proxy, responding with its own MAC address for radio client IPs.
 * This allows seamless Ethernet<->Radio bridging without requiring each radio client to
 * answer ARP requests directly.
 */
static void ProcessARPPacket(uint8_t *data, uint16_t size)
{
    uint32_t ARP_client_IP;
    uint32_t ARP_requested_IP;
    uint8_t ARP_client_MAC[6];
    int answer_needed = 0;
    int is_inside_subnet = 0;
    int is_inside_client_range = 0;
    uint8_t ARP_reply[50];
    
    /* ARP request format (after Ethernet header):
     * Offset +14: Hardware type (2 bytes) = 0x0001 (Ethernet)
     * Offset +16: Protocol type (2 bytes) = 0x0800 (IPv4)
     * Offset +18: Hardware size (1 byte) = 0x06
     * Offset +19: Protocol size (1 byte) = 0x04
     * Offset +20: Opcode (2 bytes) = 0x0001 (request), 0x0002 (reply)
     * Offset +22: Sender MAC (6 bytes)
     * Offset +28: Sender IP (4 bytes)
     * Offset +32: Target MAC (6 bytes) - unknown in request
     * Offset +38: Target IP (4 bytes)
     */
    
    /* Verify this is an ARP request */
    if (size < 42 || data[20] != 0x00 || data[21] != 0x01) {
        return;  /* Not an ARP request */
    }
    
    /* Extract sender (client) IP and MAC */
    ARP_client_IP = IP_CharToInt(data + 28);
    for (int i = 0; i < 6; i++) {
        ARP_client_MAC[i] = data[22 + i];
    }
    
    /* Extract requested (target) IP */
    ARP_requested_IP = IP_CharToInt(data + 38);
    
    /* Don't reply if requesting modem's own IP */
    if (ARP_requested_IP == LAN_conf_applied.LAN_modem_IP) {
        return;
    }
    
    /* Determine if modem should reply (act as proxy) */
    if (is_TDMA_master) {
        /* TDMA Master: answer for all IPs in radio range */
        if ((ARP_requested_IP >= CONF_radio_IP_start) &&
            (ARP_requested_IP < (CONF_radio_IP_start + CONF_radio_IP_size))) {
            answer_needed = 1;
        }
    } else {
        /* TDMA Client: answer for IPs inside subnet but outside own DHCP range */
        /* Check if requested IP is inside subnet */
        if ((ARP_requested_IP & LAN_conf_applied.LAN_subnet_mask) ==
            (LAN_conf_applied.LAN_modem_IP & LAN_conf_applied.LAN_subnet_mask)) {
            is_inside_subnet = 1;
        }
        
        /* Check if requested IP is in client's DHCP range */
        if ((ARP_requested_IP >= LAN_conf_applied.DHCP_range_start) &&
            (ARP_requested_IP < (LAN_conf_applied.DHCP_range_start + LAN_conf_applied.DHCP_range_size))) {
            is_inside_client_range = 1;
        }
        
        /* Answer if inside subnet but outside client range (route via master) */
        if (is_inside_subnet && !is_inside_client_range) {
            answer_needed = 1;
        }
    }
    
    if (answer_needed) {
        /* Build ARP reply packet */
        /* Ethernet header */
        memcpy(ARP_reply + 0, ARP_client_MAC, 6);      /* Dest MAC = requester */
        memcpy(ARP_reply + 6, CONF_modem_MAC, 6);      /* Src MAC = modem */
        ARP_reply[12] = 0x08;                          /* Ethertype */
        ARP_reply[13] = 0x06;                          /* = ARP */
        
        /* ARP header */
        ARP_reply[14] = 0x00; ARP_reply[15] = 0x01;    /* HW type = Ethernet */
        ARP_reply[16] = 0x08; ARP_reply[17] = 0x00;    /* Protocol = IPv4 */
        ARP_reply[18] = 0x06;                          /* HW addr size */
        ARP_reply[19] = 0x04;                          /* Protocol addr size */
        ARP_reply[20] = 0x00; ARP_reply[21] = 0x02;    /* Opcode = reply */
        
        /* ARP payload */
        memcpy(ARP_reply + 22, CONF_modem_MAC, 6);     /* Sender MAC = modem */
        IP_IntToChar(ARP_requested_IP, ARP_reply + 28); /* Sender IP = requested IP */
        memcpy(ARP_reply + 32, ARP_client_MAC, 6);     /* Target MAC = requester */
        IP_IntToChar(ARP_client_IP, ARP_reply + 38);   /* Target IP = requester IP */
        
        /* Send ARP reply via RAW socket */
        if (hw5500 != NULL) {
            W5500_SendData(hw5500, 0, ARP_reply, 42);  /* Socket 0 = RAW */
            W5500_ExecCommand(hw5500, 0, 0x20);        /* SEND command */
        }
        
        arp_packet_count++;
    }
}

/**
 * @brief Process IPv4 packet
 * @param data Pointer to Ethernet frame
 * @param size Frame size
 * @note Handles FDD downlink (UDP port 6716) and normal IPv4 routing
 */
static void ProcessIPv4Packet(uint8_t *data, uint16_t size)
{
    uint8_t *ip_header = data + 14;  /* Skip Ethernet header */
    uint8_t protocol = ip_header[9];  /* Protocol field */
    uint8_t ip_header_len = (ip_header[0] & 0x0F) * 4;  /* IHL field * 4 bytes */
    uint32_t dest_IP_addr;
    uint16_t src_port = 0, dst_port = 0;
    uint16_t udp_length;
    uint8_t *udp_payload;
    uint16_t payload_size;
    
    /* Extract destination IP */
    dest_IP_addr = IP_CharToInt(ip_header + 16);  /* Offset 16 in IP header */
    
    /* Check for UDP packets */
    if (protocol == IP_PROTO_UDP && size >= (14 + ip_header_len + 8)) {
        uint8_t *udp_header = ip_header + ip_header_len;
        src_port = (udp_header[0] << 8) | udp_header[1];
        dst_port = (udp_header[2] << 8) | udp_header[3];
        udp_length = (udp_header[4] << 8) | udp_header[5];
        
        /* FDD Downlink: UDP packets to modem's IP on port 6716 */
        /* Reference: source/Eth_IPv4.cpp lines 107-122 */
        if (is_TDMA_master && 
            CONF_radio.master_FDD == 1 && 
            dest_IP_addr == LAN_conf_applied.LAN_modem_IP && 
            dst_port == FDD_DOWN_PORT) {
            
            /* Extract UDP payload (skip UDP header = 8 bytes) */
            udp_payload = udp_header + 8;
            payload_size = udp_length - 8;  /* UDP length includes header */
            
            /* Sanity check payload size */
            if (payload_size > 0 && payload_size <= 400) {
                /* Inject into radio RX path */
                InjectFDDDownlink(udp_payload, payload_size);
                return;  /* FDD downlink handled, don't route normally */
            }
        }
    }
    
    /* Normal IPv4 routing to radio */
    RouteIPv4ToRadio(data, size);
}

/**
 * @brief Inject FDD downlink packet into radio RX path
 * @param udp_payload Pointer to UDP payload (raw radio packet data)
 * @param payload_size Size of UDP payload
 * @note Reference: source/L1L2_radio.cpp — FDDdown_RX_pckt_treat()
 * 
 * FDD (Frequency Division Duplex) downlink allows a master modem to receive
 * downlink packets via Ethernet from another modem that's receiving them on
 * a different frequency. The UDP payload contains a raw radio packet that
 * is injected into the RX FIFO as if it was received from the SI4463.
 */
static void InjectFDDDownlink(uint8_t *udp_payload, uint16_t payload_size)
{
    RadioISREvent_t event;
    
    /* Sanity check: payload should contain at least frame_timer(3) + RSSI(1) + length(1) = 5 bytes */
    if (payload_size < 5 || payload_size > 400) {
        printf("FDD downlink: invalid payload size %u\r\n", payload_size);
        return;
    }
    
    /* Write payload to RX FIFO starting at offset 0 */
    RX_FIFO_Write(0, udp_payload, payload_size);
    
    /* Reset read pointer and set last_received to trigger processing */
    RX_FIFO_RD_point = 0;
    RX_FIFO_last_received = payload_size;
    
    /* Notify radio task to process the injected packet */
    event.event_type = 0;  /* RX event */
    event.timestamp = HAL_GetTick();
    xQueueSend(xRadioISRQueue, &event, 0);  /* Non-blocking send */
    
    printf("FDD downlink: injected %u bytes into RX FIFO\r\n", payload_size);
}

/**
 * @brief Route IPv4 packet to radio
 * @param eth_frame Pointer to Ethernet frame (starts with Ethernet header)
 * @param size Frame size including Ethernet header
 * @note Reference: source/Eth_IPv4.cpp — IPv4_to_radio()
 */
static void RouteIPv4ToRadio(uint8_t *eth_frame, uint16_t size)
{
    uint32_t dest_IP_addr;
    uint8_t loc_client_ID;
    int MAC_dest_match = 1;
    int is_inside_subnet = 0;
    int is_inside_client_range = 0;
    int radio_tx_need = 0;
    
    /* Check if destination MAC matches modem MAC (unicast to us only) */
    for (int i = 0; i < 6; i++) {
        if (eth_frame[i] != CONF_modem_MAC[i]) {
            MAC_dest_match = 0;
            break;
        }
    }
    
    if (MAC_dest_match == 0) {
        return;  /* Not for us (broadcast/multicast or other device) */
    }
    
    /* Extract destination IP from IP header (offset +30 from Ethernet start) */
    dest_IP_addr = IP_CharToInt(eth_frame + 30);
    
    if (is_TDMA_master && (dest_IP_addr != LAN_conf_applied.LAN_modem_IP)) {
        /* TDMA Master: route to radio client if IP is in radio range */
        if ((dest_IP_addr >= CONF_radio_IP_start) && 
            (dest_IP_addr < (CONF_radio_IP_start + CONF_radio_IP_size))) {
            loc_client_ID = LookupClientIDFromIP(dest_IP_addr);
            if (loc_client_ID < 250) {
                radio_tx_need = 1;
            }
        }
    } else if (!is_TDMA_master && (dest_IP_addr != LAN_conf_applied.LAN_modem_IP)) {
        /* TDMA Client: route packets to master under certain conditions */
        
        /* Check if destination is inside subnet */
        if ((dest_IP_addr & LAN_conf_applied.LAN_subnet_mask) == 
            (LAN_conf_applied.LAN_modem_IP & LAN_conf_applied.LAN_subnet_mask)) {
            is_inside_subnet = 1;
        }
        
        /* Check if destination is in DHCP client range */
        if ((dest_IP_addr >= LAN_conf_applied.DHCP_range_start) && 
            (dest_IP_addr < (LAN_conf_applied.DHCP_range_start + LAN_conf_applied.DHCP_range_size))) {
            is_inside_client_range = 1;
        }
        
        /* Inside subnet but outside DHCP range -> route to master */
        if (is_inside_subnet && !is_inside_client_range) {
            loc_client_ID = my_radio_client_ID;  /* Send to master (my uplink) */
            radio_tx_need = 1;
        }
        
        /* Outside subnet and IP gateway active -> route to master */
        if (!is_inside_subnet && LAN_conf_applied.LAN_def_route_activ) {
            loc_client_ID = my_radio_client_ID;  /* Send to master (my uplink) */
            radio_tx_need = 1;
        }
    }
    
    /* Send to radio if needed and radio connection is established */
    if (radio_tx_need && (my_client_radio_connexion_state == 2)) {
        /* Skip Ethernet header (14 bytes), send IP packet only */
        /* Protocol 0x02 = IPv4 access protocol */
        SegmentAndPush(eth_frame + 14, size - 14, loc_client_ID, 0x02);
    }
}

/**
 * @brief Look up client ID from IP address
 * @param ip_addr IP address to look up
 * @return Client ID (0-249) or 250 if not found
 * @note Reference: source/Eth_IPv4.cpp — lookfor_client_ID_from_IP()
 */
static uint8_t LookupClientIDFromIP(uint32_t ip_addr)
{
    uint8_t i_found = 250;
    uint32_t last_IP;
    
    for (uint8_t i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
        last_IP = CONF_radio_addr_table_IP_begin[i] + CONF_radio_addr_table_IP_size[i];
        if ((ip_addr >= CONF_radio_addr_table_IP_begin[i]) && (ip_addr < last_IP)) {
            i_found = i;
            break;
        }
    }
    
    return i_found;
}

/**
 * @brief Segment data and push to radio TX queue
 * @param data_unsegmented Pointer to unsegmented data (IP packet)
 * @param total_size Total data size
 * @param client_addr Destination client ID (or my_radio_client_ID for uplink to master)
 * @param protocol Protocol byte (0x02 for IPv4)
 * @note Reference: source/L1L2_radio.cpp — segment_and_push()
 * 
 * Segments large IP packets into 252-byte radio frames with FEC encoding.
 * Each segment contains: [client_addr+parity][protocol][segmenter_byte][data...]
 * The segmenter byte contains: [packet_counter(4 bits)][last_flag(1 bit)][reserved(1 bit)][segment_counter(3 bits)]
 */
static void SegmentAndPush(const uint8_t *data_unsegmented, int total_size, uint8_t client_addr, uint8_t protocol)
{
    static uint8_t packet_counter = 0;
    uint8_t segment_counter = 0;
    uint8_t segmenter_byte;
    uint8_t is_last_segment;
    int size_remaining;
    int segment_size;
    int size_to_send;
    int size_sent = 0;
    uint8_t data_wo_FEC[300];
    RadioRxPacket_t radio_pkt;
    int size_w_FEC;
    int size_wo_FEC;
    
    /* Sanity check */
    if (total_size > 1510 || total_size == 0) {
        return;  /* Too large or empty */
    }
    
    /* Minimum size padding */
    size_remaining = (total_size < 63) ? 63 : total_size;
    
    /* Segment the packet */
    while (size_remaining > 0) {
        /* Determine segment size (max 252 bytes data per segment) */
        if (size_remaining <= 252) {
            segment_size = size_remaining;
            is_last_segment = 0x08;  /* Bit 3 set = last segment */
        } else {
            segment_size = 252;
            is_last_segment = 0x00;
        }
        
        /* Minimum segment size is 63 bytes */
        size_to_send = (segment_size < 63) ? 63 : segment_size;
        
        /* Build segmenter byte: [packet_counter(4)][last_flag(1)][0][segment_counter(3)] */
        segmenter_byte = ((packet_counter & 0x0F) << 4) | is_last_segment | (segment_counter & 0x07);
        
        /* Build frame header (3 bytes before data) */
        data_wo_FEC[0] = client_addr + parity_bit_elab[client_addr & 0x7F];  /* Client address with parity */
        data_wo_FEC[1] = protocol;         /* Protocol: 0x02 = IPv4 */
        data_wo_FEC[2] = segmenter_byte;   /* Segmenter byte */
        
        /* Copy segment data */
        if (size_sent < total_size) {
            memcpy(data_wo_FEC + 3, data_unsegmented + size_sent, 
                   (size_sent + size_to_send <= total_size) ? size_to_send : (total_size - size_sent));
            
            /* Pad if needed */
            if (size_sent + size_to_send > total_size) {
                memset(data_wo_FEC + 3 + (total_size - size_sent), 0, size_to_send - (total_size - size_sent));
            }
        } else {
            /* Padding segment */
            memset(data_wo_FEC + 3, 0, size_to_send);
        }
        
        size_sent += size_to_send;
        
        /* FEC encode the segment */
        size_wo_FEC = size_to_send + 3;  /* +3 for header */
        size_w_FEC = FEC_Encode(data_wo_FEC, radio_pkt.data, size_wo_FEC);
        
        /* Build radio packet */
        radio_pkt.timestamp = 0;  /* Timestamp not needed for TX */
        radio_pkt.rssi = 0;       /* RSSI not needed for TX */
        radio_pkt.length = size_w_FEC;
        
        /* Push to radio TX queue (non-blocking) */
        if (xQueueSend(xRadioTxQueue, &radio_pkt, 0) != pdPASS) {
            /* Queue full - drop packet */
            tx_error_count++;
            break;
        }
        
        size_remaining -= segment_size;
        segment_counter++;
        
        /* Safety limit: max 6 segments per packet (1512 / 252 = 6) */
        if (segment_counter >= 6) {
            break;
        }
    }
    
    packet_counter++;
}

/**
 * @brief Convert IP bytes to uint32_t
 * @param ip_bytes Pointer to 4-byte IP address
 * @return IP address as uint32_t
 */
static uint32_t IP_CharToInt(const uint8_t *ip_bytes)
{
    return ((uint32_t)ip_bytes[0] << 24) |
           ((uint32_t)ip_bytes[1] << 16) |
           ((uint32_t)ip_bytes[2] << 8) |
           ((uint32_t)ip_bytes[3]);
}

/**
 * @brief Convert uint32_t to IP bytes
 * @param ip_int IP address as uint32_t
 * @param ip_char Pointer to 4-byte output buffer
 */
static void IP_IntToChar(uint32_t ip_int, uint8_t *ip_char)
{
    ip_char[0] = (ip_int >> 24) & 0xFF;
    ip_char[1] = (ip_int >> 16) & 0xFF;
    ip_char[2] = (ip_int >> 8) & 0xFF;
    ip_char[3] = ip_int & 0xFF;
}