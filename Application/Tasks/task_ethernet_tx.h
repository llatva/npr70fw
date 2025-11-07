/**
  ******************************************************************************
  * @file    task_ethernet_tx.h
  * @brief   Ethernet transmit task header
  ******************************************************************************
  */

#ifndef TASK_ETHERNET_TX_H
#define TASK_ETHERNET_TX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common.h"
#include "w5500_driver.h"
#include "stm32l4xx_hal.h"

/* Task function */
void vEthernetTxTask(void *argument);

/* Initialization function */
void EthernetTxTask_Init(W5500_Context_t *w5500_ctx);

#ifdef __cplusplus
}
#endif

#endif /* TASK_ETHERNET_TX_H */
