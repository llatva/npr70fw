/**
  ******************************************************************************
  * @file    task_ethernet.h
  * @brief   Header for combined Ethernet RX/TX task
  ******************************************************************************
  */

#ifndef TASK_ETHERNET_H
#define TASK_ETHERNET_H

#include "w5500_driver.h"

/* Function prototypes */
void EthernetTask_Init(W5500_Context_t *w5500_ctx);
void vEthernetTask(void *argument);

#endif /* TASK_ETHERNET_H */