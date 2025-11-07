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
static void RouteIPv4ToRadio(uint8_t *eth_frame, uint16_t size);
static uint32_t IP_CharToInt(const uint8_t *ip_bytes);

/**
 * @brief Initialize Ethernet task
 * @param w5500_ctx Pointer to W5500 driver context
 */
void EthernetTask_Init(W5500_Context_t *w5500_ctx)
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
            
            /* Validate packet */
            if (eth_packet.length > 0 && eth_packet.length <= sizeof(eth_packet.data)) {
                
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
 * @brief Process ARP packet
 * @param data Pointer to Ethernet frame
 * @param size Frame size
 */
static void ProcessARPPacket(uint8_t *data, uint16_t size)
{
    /* ARP processing logic - simplified for brevity */
    /* Forward to ARP handler if needed */
    /* For now, just acknowledge */
}

/**
 * @brief Process IPv4 packet
 * @param data Pointer to Ethernet frame
 * @param size Frame size
 */
static void ProcessIPv4Packet(uint8_t *data, uint16_t size)
{
    uint8_t *ip_header = data + 14;  /* Skip Ethernet header */
    uint8_t protocol = ip_header[9];  /* Protocol field */
    uint16_t src_port = 0, dst_port = 0;
    
    if (protocol == IP_PROTO_UDP) {
        uint8_t *udp_header = ip_header + (ip_header[0] & 0x0F) * 4;  /* Skip IP header */
        src_port = (udp_header[0] << 8) | udp_header[1];
        dst_port = (udp_header[2] << 8) | udp_header[3];
        
        if (dst_port == FDD_DOWN_PORT) {
            /* Route to radio */
            RouteIPv4ToRadio(data, size);
        }
    }
}

/**
 * @brief Route IPv4 packet to radio
 * @param eth_frame Pointer to Ethernet frame
 * @param size Frame size
 */
static void RouteIPv4ToRadio(uint8_t *eth_frame, uint16_t size)
{
    /* Routing logic - send to radio processing */
    /* For now, forward to radio TX queue or handler */
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