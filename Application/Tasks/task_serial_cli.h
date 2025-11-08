/*
  ******************************************************************************
  * @file    task_serial_cli.h
  * @brief   USB Serial CLI Task for NPR-70
  ******************************************************************************
  * @attention
  *
  * Port of NPR-70 modem firmware from mbed OS to FreeRTOS
  * FreeRTOS port by Lasse OH3HZB
  *
  * USB Serial console implementation using USART2 (921600 baud)
  * Reuses CLI command library shared with Telnet CLI
  *
  ******************************************************************************
  */

#ifndef TASK_SERIAL_CLI_H
#define TASK_SERIAL_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "stm32l4xx_hal.h"

/* Exported constants --------------------------------------------------------*/
#define SERIAL_CLI_TASK_STACK_SIZE    (512)
#define SERIAL_CLI_TASK_PRIORITY      (1)  /* Higher priority than Telnet */

#define SERIAL_CLI_RX_BUFFER_SIZE     (128)

/* Exported types ------------------------------------------------------------*/

/**
 * @brief Serial CLI statistics
 */
typedef struct {
    uint32_t commands_processed;
    uint32_t chars_received;
    uint32_t chars_sent;
    uint32_t rx_errors;
} SerialCLI_Stats_t;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize Serial CLI Task
 * @param huart: Pointer to UART handle (USART2)
 * @retval 0 on success, -1 on error
 */
int SerialCLI_Init(UART_HandleTypeDef *huart);

/**
 * @brief Serial CLI Task main function
 * @param argument: Not used
 */
void vSerialCLITask(void *argument);

/**
 * @brief Get Serial CLI statistics
 * @param stats: Pointer to stats structure to fill
 */
void SerialCLI_GetStats(SerialCLI_Stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* TASK_SERIAL_CLI_H */
