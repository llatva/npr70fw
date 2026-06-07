/**
  ******************************************************************************
  * @file    task_tdma.h
  * @brief   TDMA synchronization task header
  ******************************************************************************
  */

#ifndef TASK_TDMA_H
#define TASK_TDMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common.h"
#include "si4463_driver.h"
#include "stm32l4xx_hal.h"

/* Task function */
void vTDMATask(void *argument);

/* Initialization function */
void TDMATask_Init(SI4463_Context_t *si4463_ctx);

/* Utility functions */
void TDMA_NULL_frame_init(int size);
void TDMA_init_TA(uint8_t client_ID, int TA_input);
void TDMA_ProcessAllocation(const uint8_t *alloc_data, uint16_t size);

/* External TIM2 handle */
extern TIM_HandleTypeDef htim2;

#ifdef __cplusplus
}
#endif

#endif /* TASK_TDMA_H */
