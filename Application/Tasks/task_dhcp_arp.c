/**
 ******************************************************************************
 * @file    task_dhcp_arp.c
 * @brief   DHCP Server and ARP Proxy Task Implementation
 *          Handles DHCP IP allocation and ARP proxy for bridged Ethernet
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics / F4HDK NPR-70 Project
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "task_dhcp_arp.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

#include "app_common.h"
#include "w5500_driver.h"

/* Private defines -----------------------------------------------------------*/
#define DHCP_SOCKET                 0  /* W5500 socket for DHCP */
#define DHCP_SERVER_PORT            67
#define DHCP_CLIENT_PORT            68

/* DHCP Message Types */
#define DHCP_DISCOVER               1
#define DHCP_OFFER                  2
#define DHCP_REQUEST                3
#define DHCP_DECLINE                4
#define DHCP_ACK                    5
#define DHCP_NAK                    6
#define DHCP_RELEASE                7

/* Private typedef -----------------------------------------------------------*/

/**
 * @brief DHCP/ARP table entry
 */
typedef struct {
    uint8_t MAC[6];         /* MAC address */
    uint32_t IP;            /* IP address */
    uint8_t status;         /* 0=Free, 1=Allocating, 2=Allocated, 3=Timeout */
    uint32_t timestamp;     /* Last seen timestamp */
} DHCPARPEntry_t;

/* Private variables ---------------------------------------------------------*/
static DHCPARPEntry_t dhcp_arp_table[DHCP_ARP_TABLE_SIZE];
static DHCPARPStats_t stats = {0};
static W5500_Context_t *pw5500 = NULL;

/* External variables --------------------------------------------------------*/
extern SemaphoreHandle_t xSPI3Mutex;

/* Private function prototypes -----------------------------------------------*/
static int CompareIP(uint8_t *IP1, uint8_t *IP2);
static int CompareMAC(uint8_t *MAC1, uint8_t *MAC2);
static void DHCPRelease(uint8_t *client_MAC);
static int LookforFreeLANIP(uint8_t *client_MAC, uint8_t *requested_IP, 
                            uint8_t *proposed_IP, int req_type);
static void DHCPServer(void);
static void ARPProxy(uint8_t *ARP_req_packet, int size);
static void ARPRXPacketTreatment(uint8_t *ARP_RX_packet, int size);
static void DHCPARPPeriodicFreeTable(void);

/* Helper functions ----------------------------------------------------------*/

/**
 * @brief Convert 4-byte array to uint32_t IP address
 */
static uint32_t IP_char2int(uint8_t *ip_bytes) {
    return ((uint32_t)ip_bytes[0] << 24) |
           ((uint32_t)ip_bytes[1] << 16) |
           ((uint32_t)ip_bytes[2] << 8) |
           ((uint32_t)ip_bytes[3]);
}

/**
 * @brief Convert uint32_t IP address to 4-byte array
 */
static void IP_int2char(uint32_t ip, uint8_t *ip_bytes) {
    ip_bytes[0] = (ip >> 24) & 0xFF;
    ip_bytes[1] = (ip >> 16) & 0xFF;
    ip_bytes[2] = (ip >> 8) & 0xFF;
    ip_bytes[3] = ip & 0xFF;
}

/**
 * @brief Compare two IP addresses
 */
static int CompareIP(uint8_t *IP1, uint8_t *IP2) {
    for (int i = 0; i < 4; i++) {
        if (IP1[i] != IP2[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Compare two MAC addresses
 */
static int CompareMAC(uint8_t *MAC1, uint8_t *MAC2) {
    for (int i = 0; i < 6; i++) {
        if (MAC1[i] != MAC2[i]) {
            return 0;
        }
    }
    return 1;
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief Initialize DHCP/ARP Task
 * @param w5500_ctx Pointer to W5500 driver context
 */
int DHCPARPTask_Init(W5500_Context_t *w5500_ctx) {
    // Reset table
    memset(dhcp_arp_table, 0, sizeof(dhcp_arp_table));
    memset(&stats, 0, sizeof(stats));
    
    // Store W5500 context pointer
    pw5500 = w5500_ctx;
    
    return 0;
}

/**
 * @brief DHCP/ARP Task main function
 */
void vDHCPARPTask(void *argument) {
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(1000);  // 1 second period
    uint32_t periodic_counter = 0;
    
    xLastWakeTime = xTaskGetTickCount();
    
    printf("[DHCP/ARP] Task started\r\n");
    
    for (;;) {
        // Wait for next period
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        
        // Run DHCP server if enabled
        if (LAN_conf_applied.DHCP_server_active) {
            DHCPServer();
        }
        
        // Periodic table maintenance every 10 seconds
        periodic_counter++;
        if (periodic_counter >= 10) {
            periodic_counter = 0;
            DHCPARPPeriodicFreeTable();
        }
    }
}

/**
 * @brief Get DHCP/ARP task statistics
 */
void DHCPARPTask_GetStats(DHCPARPStats_t *pstats) {
    if (pstats != NULL) {
        taskENTER_CRITICAL();
        memcpy(pstats, &stats, sizeof(DHCPARPStats_t));
        taskEXIT_CRITICAL();
    }
}

/**
 * @brief Reset DHCP table
 */
void DHCP_ResetTable(void) {
    taskENTER_CRITICAL();
    for (int i = 0; i < DHCP_ARP_TABLE_SIZE; i++) {
        dhcp_arp_table[i].status = 0;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief Look for MAC address from IP address
 */
int DHCP_ARP_LookforMACFromIP(uint8_t *MAC_out, uint32_t IP_addr) {
    int found = 0;
    
    taskENTER_CRITICAL();
    for (int i = 0; i < DHCP_ARP_TABLE_SIZE; i++) {
        if ((dhcp_arp_table[i].IP == IP_addr) && (dhcp_arp_table[i].status == 2)) {
            memcpy(MAC_out, dhcp_arp_table[i].MAC, 6);
            found = 1;
            break;
        }
    }
    taskEXIT_CRITICAL();
    
    return found;
}

/**
 * @brief Print DHCP/ARP table entries
 */
void DHCP_ARP_PrintEntries(void) {
    uint8_t loc_IP_char[4];
    uint32_t current_time = GetMicrosecondTimer();
    uint32_t age_sec;
    
    printf("\r\nDHCP/ARP Table:\r\n");
    
    taskENTER_CRITICAL();
    for (int i = 0; i < DHCP_ARP_TABLE_SIZE; i++) {
        if (dhcp_arp_table[i].status != 0) {
            IP_int2char(dhcp_arp_table[i].IP, loc_IP_char);
            age_sec = (current_time - dhcp_arp_table[i].timestamp) / 1000000;
            
            printf("   %d: stat:%d IP:%d.%d.%d.%d MAC:%02X:%02X:%02X:%02X:%02X:%02X age:%lusec\r\n",
                   i, dhcp_arp_table[i].status,
                   loc_IP_char[0], loc_IP_char[1], loc_IP_char[2], loc_IP_char[3],
                   dhcp_arp_table[i].MAC[0], dhcp_arp_table[i].MAC[1],
                   dhcp_arp_table[i].MAC[2], dhcp_arp_table[i].MAC[3],
                   dhcp_arp_table[i].MAC[4], dhcp_arp_table[i].MAC[5],
                   age_sec);
        }
    }
    taskEXIT_CRITICAL();
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief Release DHCP allocation for client
 */
static void DHCPRelease(uint8_t *client_MAC) {
    for (int i = 0; i < DHCP_ARP_TABLE_SIZE; i++) {
        if (CompareMAC(dhcp_arp_table[i].MAC, client_MAC) && dhcp_arp_table[i].status) {
            dhcp_arp_table[i].status = 0;
        }
    }
}

/**
 * @brief Look for free LAN IP address
 * @param client_MAC Client MAC address
 * @param requested_IP Requested IP address
 * @param proposed_IP Output: proposed IP address
 * @param req_type Request type (1=DISCOVER, 3=REQUEST)
 * @retval 0=failed, 1=OK, 3=offer
 */
static int LookforFreeLANIP(uint8_t *client_MAC, uint8_t *requested_IP,
                            uint8_t *proposed_IP, int req_type) {
    int answer = 0;
    int i_previous_alloc = -1;
    int match_previous_alloc = 0;
    int new_alloc_entry = 0;
    uint8_t new_status = 0;
    uint32_t proposed_IP_int = 0xFFFFFFFF;
    uint32_t req_IP_int = IP_char2int(requested_IP);
    
    // 1.1) Look for client MAC in DHCP table
    for (int i = 0; i < DHCP_ARP_TABLE_SIZE; i++) {
        if (CompareMAC(dhcp_arp_table[i].MAC, client_MAC) && dhcp_arp_table[i].status) {
            i_previous_alloc = i;
        }
    }
    
    // 1.2) Check if matches previous allocation
    if (i_previous_alloc != -1) {
        match_previous_alloc = (dhcp_arp_table[i_previous_alloc].IP == req_IP_int);
        
        if (match_previous_alloc) {
            // Total match with previous allocation
            answer = 1;
            if (req_type == 1) {
                dhcp_arp_table[i_previous_alloc].status = 1;
            }
            if (req_type == 3) {
                dhcp_arp_table[i_previous_alloc].status = 2;
                dhcp_arp_table[i_previous_alloc].timestamp = GetMicrosecondTimer();
            }
        }
    }
    
    // 1.3) Check if requested IP is inside range
    int requ_IP_inside_range = 0;
    if ((req_IP_int >= LAN_conf_applied.DHCP_range_start) &&
        (req_IP_int < (LAN_conf_applied.DHCP_range_start + LAN_conf_applied.DHCP_range_size))) {
        requ_IP_inside_range = 1;
    }
    
    // 1.4) Check if requested IP is free
    int req_IP_free = 1;
    for (int i = 0; i < DHCP_ARP_TABLE_SIZE; i++) {
        if ((dhcp_arp_table[i].IP == req_IP_int) &&
            (dhcp_arp_table[i].status == 2) &&
            (i != i_previous_alloc)) {
            req_IP_free = 0;
        }
    }
    
    // 1.5) Synthesis
    int req_IP_agreed = (requ_IP_inside_range && req_IP_free);
    
    // 2.1) Free previous DHCP entry if needed
    if ((i_previous_alloc != -1) && (!match_previous_alloc) && req_IP_agreed) {
        dhcp_arp_table[i_previous_alloc].status = 0;
    }
    
    // 2.2) Look for unallocated IP
    if ((req_type == 1) && (!req_IP_agreed) && (i_previous_alloc == -1)) {
        for (int i = LAN_conf_applied.DHCP_range_size - 1; i >= 0; i--) {
            uint32_t IP_tested = LAN_conf_applied.DHCP_range_start + i;
            int OK_loc = 1;
            
            for (int j = 0; j < DHCP_ARP_TABLE_SIZE; j++) {
                if ((IP_tested == dhcp_arp_table[j].IP) && (dhcp_arp_table[j].status == 2)) {
                    OK_loc = 0;
                }
            }
            
            if (OK_loc) {
                proposed_IP_int = IP_tested;
                new_alloc_entry = 1;
                IP_int2char(proposed_IP_int, proposed_IP);
                new_status = 1;
                answer = 3;
                break;
            }
        }
    }
    
    // 2.3) Answer with previous allocation
    if ((req_type == 1) && (!req_IP_agreed) && (i_previous_alloc != -1)) {
        IP_int2char(dhcp_arp_table[i_previous_alloc].IP, proposed_IP);
        new_status = (req_type == 1) ? 1 : 2;
        if (req_type == 3) {
            dhcp_arp_table[i_previous_alloc].timestamp = GetMicrosecondTimer();
        }
        dhcp_arp_table[i_previous_alloc].status = new_status;
        answer = 3;
    }
    
    // 2.4) New IP agreed, but doesn't match previous allocation
    if ((i_previous_alloc != -1) && (!match_previous_alloc) && req_IP_agreed) {
        memcpy(proposed_IP, requested_IP, 4);
        new_alloc_entry = 1;
        new_status = (req_type == 1) ? 1 : 2;
        answer = 1;
    }
    
    // 2.5) New IP agreed and matches previous allocation
    if ((i_previous_alloc != -1) && match_previous_alloc && req_IP_agreed) {
        memcpy(proposed_IP, requested_IP, 4);
        new_status = (req_type == 1) ? 1 : 2;
        if (req_type == 3) {
            dhcp_arp_table[i_previous_alloc].timestamp = GetMicrosecondTimer();
        }
        dhcp_arp_table[i_previous_alloc].status = new_status;
        answer = 1;
    }
    
    // 2.6) New IP agreed, no previous allocation
    if ((i_previous_alloc == -1) && req_IP_agreed) {
        memcpy(proposed_IP, requested_IP, 4);
        new_alloc_entry = 1;
        new_status = (req_type == 1) ? 1 : 2;
        answer = 1;
    }
    
    // Create new DHCP entry if needed
    if (new_alloc_entry) {
        int free_DHCP_slot = -1;
        for (int i = DHCP_ARP_TABLE_SIZE - 1; i >= 0; i--) {
            if (dhcp_arp_table[i].status != 2) {
                free_DHCP_slot = i;
            }
        }
        
        if (free_DHCP_slot != -1) {
            dhcp_arp_table[free_DHCP_slot].status = new_status;
            if (new_status == 2) {
                dhcp_arp_table[free_DHCP_slot].timestamp = GetMicrosecondTimer();
            }
            dhcp_arp_table[free_DHCP_slot].IP = IP_char2int(proposed_IP);
            memcpy(dhcp_arp_table[free_DHCP_slot].MAC, client_MAC, 6);
        }
    }
    
    return answer;
}

/**
 * @brief DHCP server main function
 */
static void DHCPServer(void) {
    /* Move large buffers to SRAM2 to save main SRAM1 space (1000 bytes saved) */
    static uint8_t RX_data[600] PLACE_IN_SRAM2;
    static uint8_t DHCP_answer[400] PLACE_IN_SRAM2;
    uint8_t client_MAC[6];
    uint8_t session_ID[4];
    uint8_t requested_IP[4] = {0, 0, 0, 0};
    uint8_t proposed_IP[4];
    uint8_t message_type_client = 0;
    uint8_t message_type_server;
    int RX_size, size_UDP;
    int index_opt_answer = 240;
    int loc_status;
    
    // Check for received data on DHCP socket
    // TODO: Implement W5500_GetRxSize() and W5500_ReadUDPPacket()
    RX_size = 0;  // Stub
    
    if (RX_size > 0) {
        // TODO: Read UDP packet
        size_UDP = 0;  // Stub
        
        if (RX_data[8] == 1) {  // Valid DHCP request
            // Extract client MAC address
            memcpy(client_MAC, RX_data + 36, 6);
            
            // Extract session ID (XID)
            memcpy(session_ID, RX_data + 12, 4);
            
            // Extract requested IP
            memcpy(requested_IP, RX_data + 20, 4);
            
            // Parse DHCP options
            int option_pos = 248;
            uint8_t option_type, option_size;
            
            do {
                option_type = RX_data[option_pos];
                option_size = RX_data[option_pos + 1];
                
                switch (option_type) {
                    case 53:  // DHCP Message Type
                        message_type_client = RX_data[option_pos + 2];
                        break;
                    case 50:  // Requested IP Address
                        memcpy(requested_IP, RX_data + option_pos + 2, 4);
                        break;
                    case 54:  // DHCP Server Identifier
                        // Not used in this implementation
                        break;
                }
                
                option_pos += option_size + 2;
            } while ((option_type != 255) && (option_pos < size_UDP));
            
            // Build DHCP answer base
            memset(DHCP_answer, 0, 400);
            DHCP_answer[0] = 0x02;  // Boot Reply
            DHCP_answer[1] = 0x01;  // Ethernet
            DHCP_answer[2] = 0x06;  // Hardware address length
            DHCP_answer[3] = 0x00;  // Hops
            memcpy(DHCP_answer + 4, session_ID, 4);  // Transaction ID
            memcpy(DHCP_answer + 28, client_MAC, 6);  // Client MAC
            DHCP_answer[236] = 0x63;  // Magic cookie
            DHCP_answer[237] = 0x82;
            DHCP_answer[238] = 0x53;
            DHCP_answer[239] = 0x63;
            
            // Look for IP allocation
            loc_status = LookforFreeLANIP(client_MAC, requested_IP, proposed_IP, message_type_client);
            
            // Handle DHCP Discover
            if (message_type_client == DHCP_DISCOVER) {
                memcpy(DHCP_answer + 16, proposed_IP, 4);
                
                message_type_server = DHCP_OFFER;
                
                // Build options
                DHCP_answer[index_opt_answer++] = 53;  // Message Type
                DHCP_answer[index_opt_answer++] = 1;
                DHCP_answer[index_opt_answer++] = message_type_server;
                
                DHCP_answer[index_opt_answer++] = 1;  // Subnet Mask
                DHCP_answer[index_opt_answer++] = 4;
                IP_int2char(LAN_conf_applied.LAN_subnet_mask, DHCP_answer + index_opt_answer);
                index_opt_answer += 4;
                
                if (LAN_conf_applied.LAN_def_route_activ) {
                    DHCP_answer[index_opt_answer++] = 3;  // Router
                    DHCP_answer[index_opt_answer++] = 4;
                    IP_int2char(LAN_conf_applied.LAN_def_route, DHCP_answer + index_opt_answer);
                    index_opt_answer += 4;
                }
                
                if (LAN_conf_applied.LAN_DNS_activ) {
                    DHCP_answer[index_opt_answer++] = 6;  // DNS Server
                    DHCP_answer[index_opt_answer++] = 4;
                    IP_int2char(LAN_conf_applied.LAN_DNS_value, DHCP_answer + index_opt_answer);
                    index_opt_answer += 4;
                }
                
                DHCP_answer[index_opt_answer++] = 51;  // Lease Time
                DHCP_answer[index_opt_answer++] = 4;
                DHCP_answer[index_opt_answer++] = (DHCP_ARP_TIMEOUT >> 8) & 0xFF;
                DHCP_answer[index_opt_answer++] = DHCP_ARP_TIMEOUT & 0xFF;
                DHCP_answer[index_opt_answer++] = 0;
                DHCP_answer[index_opt_answer++] = 0;
                
                DHCP_answer[index_opt_answer++] = 54;  // DHCP Server ID
                DHCP_answer[index_opt_answer++] = 4;
                IP_int2char(LAN_conf_applied.LAN_modem_IP, DHCP_answer + index_opt_answer);
                index_opt_answer += 4;
                
                DHCP_answer[index_opt_answer++] = 255;  // End
                
                // TODO: Send DHCP packet via W5500
                // W5500_WriteTxBuffer(pw5500, DHCP_SOCKET, DHCP_answer, index_opt_answer, 1);
                
                taskENTER_CRITICAL();
                stats.dhcp_offers++;
                taskEXIT_CRITICAL();
            }
            
            // Handle DHCP Request
            if ((message_type_client == DHCP_REQUEST) && (loc_status != 0)) {
                memcpy(DHCP_answer + 16, proposed_IP, 4);
                
                message_type_server = DHCP_ACK;
                
                // Build options (same as OFFER)
                DHCP_answer[index_opt_answer++] = 53;
                DHCP_answer[index_opt_answer++] = 1;
                DHCP_answer[index_opt_answer++] = message_type_server;
                
                DHCP_answer[index_opt_answer++] = 1;
                DHCP_answer[index_opt_answer++] = 4;
                IP_int2char(LAN_conf_applied.LAN_subnet_mask, DHCP_answer + index_opt_answer);
                index_opt_answer += 4;
                
                if (LAN_conf_applied.LAN_def_route_activ) {
                    DHCP_answer[index_opt_answer++] = 3;
                    DHCP_answer[index_opt_answer++] = 4;
                    IP_int2char(LAN_conf_applied.LAN_def_route, DHCP_answer + index_opt_answer);
                    index_opt_answer += 4;
                }
                
                if (LAN_conf_applied.LAN_DNS_activ) {
                    DHCP_answer[index_opt_answer++] = 6;
                    DHCP_answer[index_opt_answer++] = 4;
                    IP_int2char(LAN_conf_applied.LAN_DNS_value, DHCP_answer + index_opt_answer);
                    index_opt_answer += 4;
                }
                
                DHCP_answer[index_opt_answer++] = 51;
                DHCP_answer[index_opt_answer++] = 4;
                DHCP_answer[index_opt_answer++] = (DHCP_ARP_TIMEOUT >> 8) & 0xFF;
                DHCP_answer[index_opt_answer++] = DHCP_ARP_TIMEOUT & 0xFF;
                DHCP_answer[index_opt_answer++] = 0;
                DHCP_answer[index_opt_answer++] = 0;
                
                DHCP_answer[index_opt_answer++] = 54;
                DHCP_answer[index_opt_answer++] = 4;
                IP_int2char(LAN_conf_applied.LAN_modem_IP, DHCP_answer + index_opt_answer);
                index_opt_answer += 4;
                
                DHCP_answer[index_opt_answer++] = 255;
                
                // TODO: Send DHCP packet
                // W5500_WriteTxBuffer(pw5500, DHCP_SOCKET, DHCP_answer, index_opt_answer, 1);
                
                taskENTER_CRITICAL();
                stats.dhcp_acks++;
                taskEXIT_CRITICAL();
            }
            
            // Handle DHCP Request NAK
            if ((message_type_client == DHCP_REQUEST) && (loc_status == 0)) {
                message_type_server = DHCP_NAK;
                
                DHCP_answer[index_opt_answer++] = 53;
                DHCP_answer[index_opt_answer++] = 1;
                DHCP_answer[index_opt_answer++] = message_type_server;
                DHCP_answer[index_opt_answer++] = 255;
                
                // TODO: Send DHCP NAK
                // W5500_WriteTxBuffer(pw5500, DHCP_SOCKET, DHCP_answer, index_opt_answer, 1);
                
                taskENTER_CRITICAL();
                stats.dhcp_naks++;
                taskEXIT_CRITICAL();
            }
            
            // Handle DHCP Release
            if (message_type_client == DHCP_RELEASE) {
                DHCPRelease(client_MAC);
                
                taskENTER_CRITICAL();
                stats.dhcp_releases++;
                taskEXIT_CRITICAL();
            }
        }
    }
}

/**
 * @brief ARP proxy function
 */
static void ARPProxy(uint8_t *ARP_req_packet, int size) {
    // TODO: Implement ARP proxy for bridged Ethernet emulation
    // This function answers ARP requests for IPs in the radio range
    // to enable transparent routing between Ethernet and radio
}

/**
 * @brief ARP packet treatment
 */
static void ARPRXPacketTreatment(uint8_t *ARP_RX_packet, int size) {
    // TODO: Process received ARP packets
    // Update ARP table with sender's MAC/IP mapping
}

/**
 * @brief Periodic table cleanup
 */
static void DHCPARPPeriodicFreeTable(void) {
    uint32_t current_time = GetMicrosecondTimer();
    uint32_t timeout_us = DHCP_ARP_TIMEOUT * 1000000UL;
    int freed = 0;
    
    taskENTER_CRITICAL();
    for (int i = 0; i < DHCP_ARP_TABLE_SIZE; i++) {
        if (dhcp_arp_table[i].status == 2) {
            if ((current_time - dhcp_arp_table[i].timestamp) > timeout_us) {
                dhcp_arp_table[i].status = 0;
                freed++;
            }
        }
    }
    
    if (freed > 0) {
        stats.table_timeouts += freed;
    }
    
    // Update current table entry count
    stats.table_entries = 0;
    for (int i = 0; i < DHCP_ARP_TABLE_SIZE; i++) {
        if (dhcp_arp_table[i].status != 0) {
            stats.table_entries++;
        }
    }
    taskEXIT_CRITICAL();
}
