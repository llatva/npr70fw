/**
  ******************************************************************************
  * @file    task_monitor.c
  * @brief   System Monitor Task Implementation
  *          Periodic health checks: temperature monitoring, recalibration,
  *          stack monitoring, LED status update
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics / F4HDK NPR-70 Project
  * SPDX-License-Identifier: GPL-3.0-or-later
  *
  ******************************************************************************
  */

#include "task_monitor.h"
#include "app_common.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

/* Private variables ---------------------------------------------------------*/
static SI4463_Context_t *psi4463 = NULL;
static MonitorStats_t stats = {0};

/* Private function prototypes -----------------------------------------------*/
static void CheckTemperature(void);
static void CheckStackUsage(void);

/**
 * @brief Initialize Monitor Task
 */
int MonitorTask_Init(SI4463_Context_t *si4463_ctx)
{
    BaseType_t result;
    
    if (si4463_ctx == NULL) {
        return -1;
    }
    
    psi4463 = si4463_ctx;
    
    /* Create monitor task */
    result = xTaskCreate(
        vMonitorTask,
        "Monitor",
        MONITOR_TASK_STACK_SIZE,
        NULL,
        MONITOR_TASK_PRIORITY,
        NULL
    );
    
    if (result != pdPASS) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Monitor Task main function
 * @param argument: Not used
 */
void vMonitorTask(void *argument)
{
    (void)argument;
    
    /* Wait for system to stabilize */
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    for (;;) {
        /* Check temperature and trigger recalibration if needed */
        CheckTemperature();
        
        /* Monitor stack usage */
        CheckStackUsage();
        
        /* Update statistics */
        stats.check_count++;
        
        /* Sleep for check period */
        vTaskDelay(pdMS_TO_TICKS(MONITOR_CHECK_PERIOD_MS));
    }
}

/**
 * @brief Check radio temperature and trigger recalibration if needed
 */
static void CheckTemperature(void)
{
    uint8_t needs_recal = 0;
    HAL_StatusTypeDef status;
    
    if (psi4463 == NULL) {
        return;
    }
    
    /* Check if temperature calibration is needed */
    status = SI4463_CheckTemperatureCalibration(psi4463, &needs_recal);
    
    if (status == HAL_OK) {
        /* Read current temperature for statistics */
        int16_t temp = SI4463_ReadTemperature(psi4463);
        if (temp != 0xFFFF) {
            stats.last_temperature = temp;
        }
        
        if (needs_recal) {
            /* Temperature changed significantly - recalibration needed */
            /* Note: SI4463_CheckTemperatureCalibration already updated last_temperature */
            stats.recal_count++;
            
            /* TODO: Implement actual recalibration sequence if needed */
            /* The SI4463 automatically compensates for temperature changes in most cases */
            /* Manual recalibration may require reloading configuration or changing state */
            
            printf("[Monitor] Temperature recalibration needed (T=%d°C, count=%lu)\r\n", 
                   temp, stats.recal_count);
        }
    }
}

/**
 * @brief Check stack usage for all tasks
 */
static void CheckStackUsage(void)
{
    UBaseType_t stack_free;
    
    /* Check this task's stack */
    stack_free = uxTaskGetStackHighWaterMark(NULL);
    
    if (stats.min_stack_free == 0 || stack_free < stats.min_stack_free) {
        stats.min_stack_free = stack_free;
    }
    
    /* Optional: Check other tasks' stack usage */
    /* This can be extended to iterate through all tasks if needed */
}

/**
 * @brief Get monitor task statistics
 */
void MonitorTask_GetStats(MonitorStats_t *stats_out)
{
    if (stats_out != NULL) {
        stats_out->check_count = stats.check_count;
        stats_out->recal_count = stats.recal_count;
        stats_out->last_temperature = stats.last_temperature;
        stats_out->min_stack_free = stats.min_stack_free;
    }
}
