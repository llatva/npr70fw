/**
 ******************************************************************************
 * @file    task_dhcp_arp.h
 * @brief   DHCP Server and ARP Proxy Task Header
 *          Handles DHCP IP allocation and ARP proxy for bridged Ethernet
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics / F4HDK NPR-70 Project
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 ******************************************************************************
 */

#ifndef TASK_DHCP_ARP_H
#define TASK_DHCP_ARP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "w5500_driver.h"

/* Exported constants --------------------------------------------------------*/
#define DHCP_ARP_TASK_STACK_SIZE    (256)
#define DHCP_ARP_TASK_PRIORITY      (2)  /* Lower priority service task */
#define DHCP_ARP_TABLE_SIZE         (16) /* Max DHCP/ARP entries (reduced from 32) */
#define DHCP_ARP_TIMEOUT            (360) /* Timeout in seconds */

/* Exported types ------------------------------------------------------------*/

/**
 * @brief DHCP/ARP task statistics
 */
typedef struct {
    uint32_t dhcp_discovers;    /* DHCP Discover messages received */
    uint32_t dhcp_requests;     /* DHCP Request messages received */
    uint32_t dhcp_releases;     /* DHCP Release messages received */
    uint32_t dhcp_offers;       /* DHCP Offer messages sent */
    uint32_t dhcp_acks;         /* DHCP ACK messages sent */
    uint32_t dhcp_naks;         /* DHCP NAK messages sent */
    uint32_t arp_requests;      /* ARP requests received */
    uint32_t arp_replies;       /* ARP replies sent */
    uint32_t arp_learned;       /* ARP entries learned from packets */
    uint32_t table_entries;     /* Current DHCP/ARP table entries */
    uint32_t table_timeouts;    /* Table entry timeouts */
} DHCPARPStats_t;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize DHCP/ARP Task
 * @param w5500_ctx Pointer to W5500 driver context
 * @retval 0 on success, -1 on error
 */
int DHCPARPTask_Init(W5500_Context_t *w5500_ctx);

/**
 * @brief DHCP/ARP Task main function
 * @param argument: Not used
 */
void vDHCPARPTask(void *argument);

/**
 * @brief Get DHCP/ARP task statistics
 * @param stats: Pointer to stats structure to fill
 */
void DHCPARPTask_GetStats(DHCPARPStats_t *stats);

/**
 * @brief Reset DHCP table
 */
void DHCP_ResetTable(void);

/**
 * @brief Look for MAC address from IP address
 * @param MAC_out: Output buffer for MAC address (6 bytes)
 * @param IP_addr: IP address to search for
 * @retval 1 if found, 0 if not found
 */
int DHCP_ARP_LookforMACFromIP(uint8_t *MAC_out, uint32_t IP_addr);

/**
 * @brief Print DHCP/ARP table entries (for debugging)
 */
void DHCP_ARP_PrintEntries(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_DHCP_ARP_H */
