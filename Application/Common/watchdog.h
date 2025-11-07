/*
  ******************************************************************************
  * @file    watchdog.h
  * @brief   Independent Watchdog (IWDG) management for system reliability
  ******************************************************************************
  * @attention
  *
  * The IWDG runs on an independent 32 kHz LSI clock and provides
  * automatic system reset if not refreshed within the timeout period.
  *
  ******************************************************************************
  */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/* Exported defines ----------------------------------------------------------*/
/* Watchdog timeout: ~5 seconds @ 32kHz LSI */
#define IWDG_TIMEOUT_MS         5000

/* Task watchdog timeout (per task, in milliseconds) */
#define TASK_WATCHDOG_TIMEOUT   10000

/* Maximum number of monitored tasks */
#define MAX_MONITORED_TASKS     10

/* Exported types ------------------------------------------------------------*/

/**
 * @brief Task watchdog entry
 */
typedef struct {
    TaskHandle_t task_handle;
    const char *task_name;
    uint32_t last_checkin;
    uint32_t timeout_ms;
    uint8_t enabled;
} TaskWatchdogEntry_t;

/* Exported variables --------------------------------------------------------*/
extern IWDG_HandleTypeDef hiwdg;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize the independent watchdog
 * @return HAL_OK if successful, HAL_ERROR otherwise
 */
HAL_StatusTypeDef Watchdog_Init(void);

/**
 * @brief Refresh the watchdog timer (must be called periodically)
 */
void Watchdog_Refresh(void);

/**
 * @brief Register a task for watchdog monitoring
 * @param task_handle FreeRTOS task handle
 * @param task_name Task name for debugging
 * @param timeout_ms Timeout in milliseconds
 * @return HAL_OK if registered, HAL_ERROR if table full
 */
HAL_StatusTypeDef Watchdog_RegisterTask(TaskHandle_t task_handle, 
                                        const char *task_name,
                                        uint32_t timeout_ms);

/**
 * @brief Task check-in (call periodically from each monitored task)
 * @param task_handle Task handle (use xTaskGetCurrentTaskHandle())
 */
void Watchdog_TaskCheckin(TaskHandle_t task_handle);

/**
 * @brief Check all monitored tasks for timeouts
 * @return HAL_OK if all tasks healthy, HAL_ERROR if timeout detected
 */
HAL_StatusTypeDef Watchdog_CheckTasks(void);

/**
 * @brief Get task watchdog status string for debugging
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of bytes written
 */
int Watchdog_GetStatus(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* WATCHDOG_H */
