/**
  ******************************************************************************
  * @file    task_ethernet_rx.h
  * @brief   Ethernet receive task header
  ******************************************************************************
  */

#ifndef TASK_ETHERNET_RX_H
#define TASK_ETHERNET_RX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common.h"
#include "w5500_driver.h"
#include "stm32l4xx_hal.h"

/* Task function */
void vEthernetRxTask(void *argument);

/* Initialization function */
void EthernetRxTask_Init(W5500_Context_t *w5500_ctx);

#ifdef __cplusplus
}
#endif

#endif /* TASK_ETHERNET_RX_H */
