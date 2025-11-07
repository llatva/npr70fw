/*
  ******************************************************************************
  * @file    task_telnet.h
  * @brief   Telnet HMI Task for NPR-70
  ******************************************************************************
  * @attention
  *
  * Port of NPR-70 modem firmware from mbed OS to FreeRTOS
  * Original copyright (c) 2017-2020 Guillaume F. F4HDK
  * FreeRTOS port by Lasse OH3HZB
  *
  * Minimal telnet console implementation on TCP port 23
  * Note: Full command set from original (963 lines) deferred due to RAM constraints
  *
  ******************************************************************************
  */

#ifndef TASK_TELNET_H
#define TASK_TELNET_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "w5500_driver.h"

/* Exported constants --------------------------------------------------------*/
#define TELNET_TASK_STACK_SIZE    (256)
#define TELNET_TASK_PRIORITY      (0)  /* Lowest priority service task */

#define TELNET_INACTIVITY_TIMEOUT (300000000)  /* 5 minutes in microseconds */
#define TELNET_MAX_LINE_LEN       (100)

/* Exported types ------------------------------------------------------------*/

/**
 * @brief Telnet task statistics
 */
typedef struct {
    uint32_t connections_total;
    uint32_t connections_active;
    uint32_t commands_processed;
    uint32_t timeouts;
} TelnetStats_t;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize Telnet Task
 * @param w5500_ctx Pointer to W5500 driver context
 * @retval 0 on success, -1 on error
 */
int TelnetTask_Init(W5500_Context_t *w5500_ctx);

/**
 * @brief Telnet Task main function
 * @param argument: Not used
 */
void vTelnetTask(void *argument);

/**
 * @brief Get Telnet task statistics
 * @param stats: Pointer to stats structure to fill
 */
void TelnetTask_GetStats(TelnetStats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* TASK_TELNET_H */
