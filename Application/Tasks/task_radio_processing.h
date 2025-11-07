/**
  ******************************************************************************
  * @file    task_radio_processing.h
  * @brief   Radio packet processing task header
  ******************************************************************************
  */

#ifndef TASK_RADIO_PROCESSING_H
#define TASK_RADIO_PROCESSING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common.h"
#include "w5500_driver.h"
#include "stm32l4xx_hal.h"

/* Task function */
void vRadioProcessingTask(void *argument);

/* Initialization function */
void RadioProcessingTask_Init(W5500_Context_t *w5500_ctx);

/* Diagnostics: expose buffer pointers and last-used timestamps */
extern uint8_t *ethernet_buffer[];
extern uint32_t buffer_last_used_ms[];

#ifdef __cplusplus
}
#endif

#endif /* TASK_RADIO_PROCESSING_H */
