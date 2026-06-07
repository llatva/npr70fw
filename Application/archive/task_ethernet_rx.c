/**
  ******************************************************************************
  * @file    task_ethernet_rx.c
  * @brief   Ethernet receive task - poll W5500 and process incoming packets
  ******************************************************************************
  * @attention
  *
  * This task polls the W5500 for incoming Ethernet frames and processes them.
  * Handles ARP, IPv4 routing to radio, and other protocols.
  * Runs at priority 5 (high priority, time-sensitive for network).
  *
  ******************************************************************************
  */

#include "task_ethernet_rx.h"
#include "app_common.h"
#include "w5500_driver.h"
#include <stdio.h>
#include <string.h>

/* Private defines -----------------------------------------------------------*/
#define ETHERTYPE_ARP       0x0806
#define ETHERTYPE_IPV4      0x0800
#define IP_PROTO_UDP        0x11
#define FDD_DOWN_PORT       0x1A3C  /* 6716 decimal */

/* Private variables ---------------------------------------------------------*/
static W5500_Context_t *hw5500 = NULL;

/* RX buffer - allocate from heap to save BSS space */
static uint8_t *rx_buffer = NULL;

/* Statistics */
static uint32_t rx_packet_count = 0;
static uint32_t arp_packet_count = 0;
static uint32_t ipv4_packet_count = 0;

/* Private function prototypes -----------------------------------------------*/
static void ProcessARPPacket(uint8_t *data, uint16_t size);
static void ProcessIPv4Packet(uint8_t *data, uint16_t size);
static void RouteIPv4ToRadio(uint8_t *eth_frame, uint16_t size);
static uint32_t IP_CharToInt(const uint8_t *ip_bytes);

/**
 * @brief Initialize Ethernet RX task
 * @param w5500_ctx Pointer to W5500 driver context
 */
void EthernetRxTask_Init(W5500_Context_t *w5500_ctx)
{
    hw5500 = w5500_ctx;
    
    /* Allocate RX buffer from heap to save static RAM */
    rx_buffer = (uint8_t *)pvPortMalloc(1600);
    if (rx_buffer == NULL) {
        /* Allocation failed - critical error */
        while(1);  /* Trap */
    }
}

/**
 * @brief Ethernet RX task - poll and process incoming packets
 * @param argument Not used
 */
void vEthernetRxTask(void *argument)
{
    uint16_t rx_size;
    uint16_t ethertype;
    HAL_StatusTypeDef status;
    
    printf("EthernetRx task started\r\n");
    
    /* Ensure buffer is allocated */
    if (rx_buffer == NULL) {
        printf("ERROR: RX buffer not allocated\r\n");
        vTaskDelete(NULL);
    }
    
    for (;;) {
        /* Check for received data on RAW socket */
        rx_size = W5500_GetRxSize(hw5500, W5500_SOCK_RAW);
        
        if (rx_size > 0) {
            /* Limit size to buffer capacity */
            if (rx_size > 1600) {
                rx_size = 1600;
            }
            
            /* Receive packet from W5500 */
            status = W5500_RecvData(hw5500, W5500_SOCK_RAW, rx_buffer, rx_size);
            
            if (status == HAL_OK && rx_size >= 16) {  /* Minimum Ethernet header + type */
                
                rx_packet_count++;
                
                /* Extract EtherType (bytes 14-15, after 14-byte Ethernet header) */
                ethertype = (rx_buffer[14] << 8) | rx_buffer[15];
                
                /* Process based on EtherType */
                switch (ethertype) {
                    
                    case ETHERTYPE_ARP:  /* 0x0806 - ARP */
                        arp_packet_count++;
                        /* Skip 2-byte header from W5500 MAC read */
                        ProcessARPPacket(rx_buffer + 2, rx_size - 2);
                        break;
                        
                    case ETHERTYPE_IPV4:  /* 0x0800 - IPv4 */
                        ipv4_packet_count++;
                        ProcessIPv4Packet(rx_buffer + 2, rx_size - 2);
                        break;
                        
                    default:
                        /* Unknown protocol - ignore */
                        break;
                }
            }
        } else {
            /* No data - sleep briefly to avoid busy loop */
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

/**
 * @brief Process ARP packet
 * @param data Ethernet frame data (without W5500 header)
 * @param size Frame size
 */
static void ProcessARPPacket(uint8_t *data, uint16_t size)
{
    /* TODO: Implement ARP processing
     * - ARP request/reply handling
     * - Update ARP cache
     * - Send ARP replies for our IP
     * 
     * For now, forward to DHCP_ARP task via queue
     */
    
    /* In master FDD uplink mode, ignore ARP */
    if (is_TDMA_master && CONF_radio.master_FDD >= 2) {
        return;
    }
    
    /* TODO: Queue to DHCP_ARP task or process directly */
    /* For now, just count it */
}

/**
 * @brief Process IPv4 packet
 * @param data Ethernet frame data (without W5500 header)
 * @param size Frame size
 */
static void ProcessIPv4Packet(uint8_t *data, uint16_t size)
{
    uint16_t dest_port;
    uint8_t protocol;
    uint32_t dest_ip;
    
    /* Minimum IPv4 packet: 14 (Eth) + 20 (IPv4) + 8 (UDP) = 42 bytes */
    if (size < 42) {
        return;
    }
    
    /* Extract IP protocol (byte 23 = offset 9 in IPv4 header) */
    protocol = data[23];
    
    /* Extract destination IP (bytes 30-33) */
    dest_ip = IP_CharToInt(data + 30);
    
    /* Extract destination port for UDP (bytes 36-37) */
    if (protocol == IP_PROTO_UDP && size >= 42) {
        dest_port = (data[36] << 8) | data[37];
    } else {
        dest_port = 0;
    }
    
    /* Check if this is FDD downlink data (master mode only) */
    if (is_TDMA_master && CONF_radio.master_FDD == 1) {
        /* Master FDD downlink mode */
        if (protocol == IP_PROTO_UDP && 
            dest_ip == LAN_conf_applied.LAN_modem_IP && 
            dest_port == FDD_DOWN_PORT) {
            
            /* This is FDD downlink data - handle specially */
            /* TODO: Process FDD downlink packet (put in radio RX FIFO) */
            /* For now, skip */
            return;
        }
    }
    
    /* Normal IPv4 packet - route to radio */
    RouteIPv4ToRadio(data, size);
}

/**
 * @brief Route IPv4 packet to radio transmission
 * @param eth_frame Ethernet frame (without W5500 header)
 * @param size Frame size
 */
static void RouteIPv4ToRadio(uint8_t *eth_frame, uint16_t size)
{
    /* TODO: Implement IPv4 to radio routing
     * This should:
     * 1. Extract destination IP
     * 2. Lookup in radio address table to find client ID
     * 3. Segment packet if needed (max ~300 bytes per radio frame)
     * 4. Queue to radio TX task
     * 
     * For now, this is a stub - full implementation needed when
     * radio TX path is complete
     */
    
    /* Placeholder: Just count the packet */
    (void)eth_frame;
    (void)size;
}

/**
 * @brief Convert IP address from byte array to uint32
 * @param ip_bytes 4-byte IP address array
 * @return IP address as 32-bit integer
 */
static uint32_t IP_CharToInt(const uint8_t *ip_bytes)
{
    return ((uint32_t)ip_bytes[0] << 24) |
           ((uint32_t)ip_bytes[1] << 16) |
           ((uint32_t)ip_bytes[2] << 8) |
           ((uint32_t)ip_bytes[3]);
}

/**
 * @brief Get Ethernet RX statistics
 * @param rx_count Pointer to store received packet count
 * @param arp_count Pointer to store ARP packet count
 * @param ipv4_count Pointer to store IPv4 packet count
 */
void EthernetRxTask_GetStats(uint32_t *rx_count, uint32_t *arp_count, uint32_t *ipv4_count)
{
    if (rx_count) {
        *rx_count = rx_packet_count;
    }
    if (arp_count) {
        *arp_count = arp_packet_count;
    }
    if (ipv4_count) {
        *ipv4_count = ipv4_packet_count;
    }
}
