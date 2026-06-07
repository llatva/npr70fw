/**
  ******************************************************************************
  * @file    task_monitor.h
  * @brief   System Monitor Task Header
  *          Periodic health checks, temperature monitoring, stack monitoring
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics / F4HDK NPR-70 Project
  * SPDX-License-Identifier: GPL-3.0-or-later
  *
  ******************************************************************************
  */

#ifndef TASK_MONITOR_H
#define TASK_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "si4463_driver.h"

/* Exported constants --------------------------------------------------------*/
#define MONITOR_TASK_STACK_SIZE    (128)  /* 128 words = 512 bytes */
#define MONITOR_TASK_PRIORITY      (1)    /* Lowest priority */
#define MONITOR_CHECK_PERIOD_MS    (30000) /* 30 seconds */

/* Exported types ------------------------------------------------------------*/

/**
 * @brief Monitor task statistics
 */
typedef struct {
    uint32_t check_count;           /* Number of checks performed */
    uint32_t recal_count;           /* Number of recalibrations triggered */
    int16_t last_temperature;       /* Last temperature reading (°C) */
    uint32_t min_stack_free;        /* Minimum free stack seen (words) */
} MonitorStats_t;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize Monitor Task
 * @param si4463_ctx Pointer to SI4463 driver context
 * @retval 0 on success, -1 on error
 */
int MonitorTask_Init(SI4463_Context_t *si4463_ctx);

/**
 * @brief Monitor Task main function
 * @param argument: Not used
 */
void vMonitorTask(void *argument);

/**
 * @brief Get monitor task statistics
 * @param stats: Pointer to stats structure to fill
 */
void MonitorTask_GetStats(MonitorStats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* TASK_MONITOR_H */
