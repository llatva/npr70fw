/**
  ******************************************************************************
  * @file    task_network_mgmt.h
  * @brief   Network Management Task Header (DHCP/ARP/SNMP coordinator)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics / F4HDK NPR-70 Project
  * SPDX-License-Identifier: GPL-3.0-or-later
  *
  ******************************************************************************
  */

#ifndef TASK_NETWORK_MGMT_H
#define TASK_NETWORK_MGMT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "w5500_driver.h"

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize Network Management Task
 * @param w5500_ctx Pointer to W5500 driver context
 */
void NetworkMgmtTask_Init(W5500_Context_t *w5500_ctx);

/**
 * @brief Network Management Task main function
 * @param argument: Not used
 */
void vNetworkMgmtTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* TASK_NETWORK_MGMT_H */
