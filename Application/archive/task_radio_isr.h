/**
  ******************************************************************************
  * @file    task_radio_isr.h
  * @brief   Radio interrupt handler task header
  ******************************************************************************
  */

#ifndef TASK_RADIO_ISR_H
#define TASK_RADIO_ISR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common.h"
#include "si4463_driver.h"
#include "stm32l4xx_hal.h"

/* External SPI handle */
extern SPI_HandleTypeDef hspi1;

/* Task function */
void vRadioISRHandlerTask(void *argument);

/* Initialization function */
void RadioISRTask_Init(SI4463_Context_t *si4463_ctx);

#ifdef __cplusplus
}
#endif

#endif /* TASK_RADIO_ISR_H */
