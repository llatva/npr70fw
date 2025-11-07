/*
  ******************************************************************************
  * @file    watchdog.c
  * @brief   Independent Watchdog implementation
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "watchdog.h"
#include <string.h>
#include <stdio.h>

/* Private variables ---------------------------------------------------------*/
IWDG_HandleTypeDef hiwdg;
static TaskWatchdogEntry_t task_watchdog_table[MAX_MONITORED_TASKS];
static uint8_t task_count = 0;
static uint8_t iwdg_started = 0;  /* Flag to track if IWDG has been started */

/* Private functions ---------------------------------------------------------*/

/**
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{
    /* IWDG Configuration:
     * LSI clock: ~32 kHz
     * Prescaler: 32 (gives ~1 ms per tick)
     * Reload: 5000 (gives ~5 second timeout)
     */
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 4095;  /* Maximum value for ~4 second timeout */
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
    {
        Error_Handler();
    }
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief Initialize the independent watchdog
 */
HAL_StatusTypeDef Watchdog_Init(void)
{
    /* Clear task table */
    memset(task_watchdog_table, 0, sizeof(task_watchdog_table));
    task_count = 0;
    iwdg_started = 0;
    
    /* Configure IWDG but DON'T start it yet - will be started after scheduler */
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 4095;  /* Maximum value for ~4 second timeout */
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    
    /* Note: IWDG will be started by first call to Watchdog_Refresh() */
    
    return HAL_OK;
}

/**
 * @brief Refresh the watchdog timer
 */
void Watchdog_Refresh(void)
{
    /* Start IWDG on first refresh (after scheduler has started) */
    if (!iwdg_started) {
        if (HAL_IWDG_Init(&hiwdg) == HAL_OK) {
            iwdg_started = 1;
        }
    }
    
    /* Refresh watchdog if it's running */
    if (iwdg_started) {
        HAL_IWDG_Refresh(&hiwdg);
    }
}

/**
 * @brief Register a task for watchdog monitoring
 */
HAL_StatusTypeDef Watchdog_RegisterTask(TaskHandle_t task_handle, 
                                        const char *task_name,
                                        uint32_t timeout_ms)
{
    if (task_count >= MAX_MONITORED_TASKS) {
        return HAL_ERROR;
    }
    
    task_watchdog_table[task_count].task_handle = task_handle;
    task_watchdog_table[task_count].task_name = task_name;
    task_watchdog_table[task_count].last_checkin = xTaskGetTickCount();
    task_watchdog_table[task_count].timeout_ms = timeout_ms;
    task_watchdog_table[task_count].enabled = 1;
    task_count++;
    
    return HAL_OK;
}

/**
 * @brief Task check-in
 */
void Watchdog_TaskCheckin(TaskHandle_t task_handle)
{
    uint32_t now = xTaskGetTickCount();
    
    for (uint8_t i = 0; i < task_count; i++) {
        if (task_watchdog_table[i].task_handle == task_handle) {
            task_watchdog_table[i].last_checkin = now;
            break;
        }
    }
}

/**
 * @brief Check all monitored tasks for timeouts
 */
HAL_StatusTypeDef Watchdog_CheckTasks(void)
{
    uint32_t now = xTaskGetTickCount();
    HAL_StatusTypeDef status = HAL_OK;
    
    for (uint8_t i = 0; i < task_count; i++) {
        if (!task_watchdog_table[i].enabled) {
            continue;
        }
        
        uint32_t elapsed = now - task_watchdog_table[i].last_checkin;
        uint32_t timeout_ticks = pdMS_TO_TICKS(task_watchdog_table[i].timeout_ms);
        
        if (elapsed > timeout_ticks) {
            /* Task timeout detected - return error but don't reset yet */
            /* Calling code can decide whether to reset or log */
            status = HAL_ERROR;
        }
    }
    
    return status;
}

/**
 * @brief Get task watchdog status string
 */
int Watchdog_GetStatus(char *buffer, size_t buffer_size)
{
    int len = 0;
    uint32_t now = xTaskGetTickCount();
    
    len += snprintf(buffer + len, buffer_size - len,
                   "Task Watchdog Status:\r\n");
    
    for (uint8_t i = 0; i < task_count && len < (int)buffer_size - 50; i++) {
        if (!task_watchdog_table[i].enabled) {
            continue;
        }
        
        uint32_t elapsed = now - task_watchdog_table[i].last_checkin;
        uint32_t elapsed_ms = elapsed * portTICK_PERIOD_MS;
        
        len += snprintf(buffer + len, buffer_size - len,
                       "  %s: %lu ms ago\r\n",
                       task_watchdog_table[i].task_name,
                       (unsigned long)elapsed_ms);
    }
    
    return len;
}
