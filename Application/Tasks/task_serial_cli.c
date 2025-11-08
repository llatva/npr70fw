/*
  ******************************************************************************
  * @file    task_serial_cli.c
  * @brief   USB Serial CLI Task Implementation
  ******************************************************************************
  * @attention
  *
  * Port of NPR-70 modem firmware from mbed OS to FreeRTOS
  * FreeRTOS port by Lasse OH3HZB
  *
  * USB Serial console implementation
  * Reuses CLI command library shared with Telnet
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "task_serial_cli.h"
#include "cli_commands.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>
#include <stdio.h>

/* Private variables ---------------------------------------------------------*/
static UART_HandleTypeDef *phuart = NULL;
static SerialCLI_Stats_t stats = {0};
static CLI_Context_t cli_ctx;
static uint8_t response_buffer[CLI_MAX_RESPONSE_LEN];

/* Private function prototypes -----------------------------------------------*/
static void SerialCLI_OutputFunc(const uint8_t *data, uint16_t len, void *user_data);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief Output function for CLI - sends data via UART
 */
static void SerialCLI_OutputFunc(const uint8_t *data, uint16_t len, void *user_data) {
    if (phuart != NULL && data != NULL && len > 0) {
        HAL_UART_Transmit(phuart, (uint8_t *)data, len, 1000);
        stats.chars_sent += len;
    }
}

/* Public Functions ----------------------------------------------------------*/

/**
 * @brief Initialize Serial CLI Task
 */
int SerialCLI_Init(UART_HandleTypeDef *huart) {
    /* Store UART handle */
    phuart = huart;
    
    /* Reset statistics */
    memset(&stats, 0, sizeof(stats));
    
    /* Initialize CLI context */
    CLI_Init(&cli_ctx, SerialCLI_OutputFunc, NULL, response_buffer, sizeof(response_buffer));
    
    return 0;
}

/**
 * @brief Serial CLI Task main function
 */
void vSerialCLITask(void *argument) {
    static uint8_t rx_char;
    static char cmd_line[CLI_MAX_LINE_LEN];
    static int cmd_pos = 0;
    HAL_StatusTypeDef status;
    
    /* Send welcome message */
    CLI_SendWelcome(&cli_ctx);
    CLI_SendPrompt(&cli_ctx);
    
    /* Task main loop */
    for (;;) {
        /* Read one character with timeout */
        status = HAL_UART_Receive(phuart, &rx_char, 1, 100);
        
        if (status == HAL_OK) {
            stats.chars_received++;
            
            /* Handle printable characters */
            if (rx_char >= 0x20 && rx_char <= 0x7E) {
                if (cmd_pos < (CLI_MAX_LINE_LEN - 1)) {
                    cmd_line[cmd_pos++] = rx_char;
                    /* Echo back */
                    HAL_UART_Transmit(phuart, &rx_char, 1, 100);
                    stats.chars_sent++;
                }
            }
            /* Handle backspace (BS or DEL) */
            else if (rx_char == 0x08 || rx_char == 0x7F) {
                if (cmd_pos > 0) {
                    cmd_pos--;
                    /* Send backspace sequence: BS, space, BS */
                    const uint8_t bs_seq[] = {0x08, 0x20, 0x08};
                    HAL_UART_Transmit(phuart, (uint8_t *)bs_seq, 3, 100);
                    stats.chars_sent += 3;
                }
            }
            /* Handle Enter/CR */
            else if (rx_char == 0x0D || rx_char == 0x0A) {
                /* Echo CR+LF */
                const uint8_t crlf[] = {0x0D, 0x0A};
                HAL_UART_Transmit(phuart, (uint8_t *)crlf, 2, 100);
                stats.chars_sent += 2;
                
                /* Null-terminate command */
                cmd_line[cmd_pos] = '\0';
                
                /* Process command if not empty */
                if (cmd_pos > 0) {
                    int result = CLI_ProcessCommand(&cli_ctx, cmd_line);
                    stats.commands_processed++;
                    
                    /* Handle special results */
                    if (result == 1) {
                        /* Exit requested - just reset for serial CLI */
                        cmd_pos = 0;
                    }
                    else if (result == 2) {
                        /* Reboot requested */
                        vTaskDelay(pdMS_TO_TICKS(100));
                        NVIC_SystemReset();
                    }
                }
                
                /* Reset command buffer */
                cmd_pos = 0;
                
                /* Send prompt */
                CLI_SendPrompt(&cli_ctx);
            }
            /* Handle Ctrl+C */
            else if (rx_char == 0x03) {
                cmd_pos = 0;
                const char *msg = "^C\r\n";
                HAL_UART_Transmit(phuart, (uint8_t *)msg, strlen(msg), 100);
                stats.chars_sent += strlen(msg);
                CLI_SendPrompt(&cli_ctx);
            }
            /* Handle Ctrl+D (logout in Unix) */
            else if (rx_char == 0x04) {
                /* Just send newline and prompt */
                const char *msg = "\r\n";
                HAL_UART_Transmit(phuart, (uint8_t *)msg, strlen(msg), 100);
                stats.chars_sent += strlen(msg);
                CLI_SendPrompt(&cli_ctx);
                cmd_pos = 0;
            }
        }
        else if (status == HAL_ERROR) {
            stats.rx_errors++;
            /* Small delay on error */
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        /* HAL_TIMEOUT is normal - just continue */
    }
}

/**
 * @brief Get Serial CLI task statistics
 */
void SerialCLI_GetStats(SerialCLI_Stats_t *pstats) {
    if (pstats != NULL) {
        memcpy(pstats, &stats, sizeof(SerialCLI_Stats_t));
    }
}
