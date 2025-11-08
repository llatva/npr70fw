/*
  ******************************************************************************
  * @file    cli_commands.h
  * @brief   Common CLI command processing library for NPR-70
  ******************************************************************************
  * @attention
  *
  * Port of NPR-70 modem firmware from mbed OS to FreeRTOS
  * Original copyright (c) 2017-2020 Guillaume F. F4HDK
  * FreeRTOS port by Lasse OH3HZB
  *
  * This module provides common command processing logic that can be used
  * by both Telnet CLI and Serial CLI tasks
  *
  ******************************************************************************
  */

#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
#define CLI_MAX_LINE_LEN       (100)
#define CLI_MAX_RESPONSE_LEN   (400)

/* Exported types ------------------------------------------------------------*/

/**
 * @brief CLI output callback function type
 * @param data: Data to send
 * @param len: Length of data
 * @param user_data: User-specific context pointer
 */
typedef void (*CLI_OutputFunc_t)(const uint8_t *data, uint16_t len, void *user_data);

/**
 * @brief CLI context structure
 */
typedef struct {
    CLI_OutputFunc_t output_func;  /* Output function callback */
    void *user_data;               /* User-specific data for callback */
    uint8_t *response_buffer;      /* Buffer for command responses */
    uint16_t response_size;        /* Size of response buffer */
} CLI_Context_t;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize CLI context
 * @param ctx: Pointer to CLI context
 * @param output_func: Output callback function
 * @param user_data: User data passed to callback
 * @param response_buffer: Buffer for responses
 * @param buffer_size: Size of response buffer
 */
void CLI_Init(CLI_Context_t *ctx, CLI_OutputFunc_t output_func, 
              void *user_data, uint8_t *response_buffer, uint16_t buffer_size);

/**
 * @brief Send welcome/banner message
 * @param ctx: Pointer to CLI context
 */
void CLI_SendWelcome(CLI_Context_t *ctx);

/**
 * @brief Send prompt
 * @param ctx: Pointer to CLI context
 */
void CLI_SendPrompt(CLI_Context_t *ctx);

/**
 * @brief Process CLI command
 * @param ctx: Pointer to CLI context
 * @param cmd: Command string (null-terminated)
 * @retval 0 = normal, 1 = exit/logout requested, 2 = reboot requested
 */
int CLI_ProcessCommand(CLI_Context_t *ctx, const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* CLI_COMMANDS_H */
