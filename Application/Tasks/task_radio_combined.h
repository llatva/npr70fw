/*
 * task_radio_combined.h
 * Combined Radio ISR + Radio Processing task (merges task_radio_isr.c and task_radio_processing.c)
 */
#ifndef TASK_RADIO_COMBINED_H
#define TASK_RADIO_COMBINED_H

#include "app_common.h"
#include "si4463_driver.h"
#include "w5500_driver.h"

/* Initialize combined radio task - provide both SI4463 and W5500 contexts */
void RadioTask_Init(SI4463_Context_t *si4463_ctx, W5500_Context_t *w5500_ctx);

/* FreeRTOS task entry */
void vRadioTask(void *argument);

/* Export HAL callback (implemented in combined task) */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

/* Reassembly buffers shared with other modules (previously in task_radio_processing.c) */
extern uint8_t *ethernet_buffer[RADIO_ADDR_TABLE_SIZE];
extern uint32_t buffer_last_used_ms[RADIO_ADDR_TABLE_SIZE];

#endif /* TASK_RADIO_COMBINED_H */
