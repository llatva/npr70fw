/*
  ******************************************************************************
  * @file    task_telnet.c
  * @brief   Telnet HMI Task Implementation
  ******************************************************************************
  * @attention
  *
  * Port of NPR-70 modem firmware from mbed to FreeRTOS
  * Original copyright (c) 2017-2020 Guillaume F. F4HDK
  * FreeRTOS port by Lasse OH3HZB
  *
  * Minimal telnet console implementation
  * TODO: Full command set implementation requires ~2-3KB additional RAM
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "task_telnet.h"
#include "cli_commands.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "w5500_driver.h"
#include "app_common.h"
#include <string.h>
#include <stdio.h>

/* Private defines -----------------------------------------------------------*/
#define TELNET_IAC              0xFF  /* Interpret As Command */
#define TELNET_WILL             0xFB
#define TELNET_DO               0xFD
#define TELNET_CMD_ECHO         0x01
#define TELNET_CMD_SUPPRESS_GA  0x03

/* Private types -------------------------------------------------------------*/
typedef enum {
    TELNET_STATE_CLOSED = 0,
    TELNET_STATE_INIT,
    TELNET_STATE_LISTEN,
    TELNET_STATE_ESTABLISHED
} TelnetState_t;

/* Private variables ---------------------------------------------------------*/
static W5500_Context_t *pw5500 = NULL;
static TelnetStats_t stats = {0};
static TelnetState_t telnet_state = TELNET_STATE_CLOSED;
static uint32_t last_activity = 0;
static CLI_Context_t cli_ctx;
/* Move large CLI response buffer to SRAM2 to save main SRAM1 space (400 bytes saved) */
static uint8_t response_buffer[CLI_MAX_RESPONSE_LEN] PLACE_IN_SRAM2;

/* External variables --------------------------------------------------------*/
extern SemaphoreHandle_t xSPI3Mutex;
extern volatile uint8_t is_telnet_active;

/* Private function prototypes -----------------------------------------------*/
static void ProcessTelnetConnection(void);
static void Telnet_OutputFunc(const uint8_t *data, uint16_t len, void *user_data);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief Output function for Telnet CLI
 */
static void Telnet_OutputFunc(const uint8_t *data, uint16_t len, void *user_data) {
    if (pw5500 != NULL && data != NULL && len > 0) {
        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
        W5500_SendData(pw5500, W5500_SOCK_TELNET, (uint8_t *)data, len);
        xSemaphoreGive(xSPI3Mutex);
    }
}

/**
 * @brief Send welcome message with telnet options
 */
static void SendWelcomeMessage(void) {
    uint8_t telnet_opts[16];
    int len = 0;
    
    /* Telnet protocol negotiation */
    telnet_opts[len++] = TELNET_IAC;
    telnet_opts[len++] = TELNET_WILL;
    telnet_opts[len++] = TELNET_CMD_ECHO;
    
    telnet_opts[len++] = TELNET_IAC;
    telnet_opts[len++] = TELNET_DO;
    telnet_opts[len++] = TELNET_CMD_SUPPRESS_GA;
    
    telnet_opts[len++] = TELNET_IAC;
    telnet_opts[len++] = TELNET_WILL;
    telnet_opts[len++] = TELNET_CMD_SUPPRESS_GA;
    
    xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
    W5500_SendData(pw5500, W5500_SOCK_TELNET, telnet_opts, len);
    xSemaphoreGive(xSPI3Mutex);
    
    /* Send welcome via CLI library */
    CLI_SendWelcome(&cli_ctx);
    CLI_SendPrompt(&cli_ctx);
}

/**
 * @brief Process telnet connection state
 */
static void ProcessTelnetConnection(void) {
    uint8_t sock_status;
    uint16_t rx_size;
    /* Move telnet session buffers to SRAM2 to save main SRAM1 space (320 bytes saved) */
    static uint8_t rx_buffer[TELNET_MAX_LINE_LEN + 10] PLACE_IN_SRAM2;
    static uint8_t tx_echo[TELNET_MAX_LINE_LEN + 10] PLACE_IN_SRAM2;
    static char cmd_line[TELNET_MAX_LINE_LEN] PLACE_IN_SRAM2;
    static int cmd_pos = 0;
    
    xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
    sock_status = W5500_GetSocketStatus(pw5500, W5500_SOCK_TELNET);
    xSemaphoreGive(xSPI3Mutex);
    
    /* State machine */
    switch (telnet_state) {
    case TELNET_STATE_CLOSED:
        /* Open socket */
        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
        W5500_WriteByte(pw5500, W5500_Sn_CR, W5500_SOCKET_REG_BLOCK(W5500_SOCK_TELNET), W5500_Sn_CR_OPEN);
        xSemaphoreGive(xSPI3Mutex);
        telnet_state = TELNET_STATE_INIT;
        break;
        
    case TELNET_STATE_INIT:
        if (sock_status == W5500_SOCK_INIT) {
            /* Start listening */
            xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
            W5500_WriteByte(pw5500, W5500_Sn_CR, W5500_SOCKET_REG_BLOCK(W5500_SOCK_TELNET), W5500_Sn_CR_LISTEN);
            xSemaphoreGive(xSPI3Mutex);
            telnet_state = TELNET_STATE_LISTEN;
        }
        break;
        
    case TELNET_STATE_LISTEN:
        if (sock_status == W5500_SOCK_ESTABLISHED) {
            /* New connection established */
            telnet_state = TELNET_STATE_ESTABLISHED;
            stats.connections_total++;
            stats.connections_active = 1;
            is_telnet_active = 1;
            cmd_pos = 0;
            last_activity = xTaskGetTickCount();
            SendWelcomeMessage();
        }
        break;
        
    case TELNET_STATE_ESTABLISHED:
        /* Check for close/timeout */
        if (sock_status == W5500_SOCK_CLOSE_WAIT) {
            xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
            W5500_WriteByte(pw5500, W5500_Sn_CR, W5500_SOCKET_REG_BLOCK(W5500_SOCK_TELNET), W5500_Sn_CR_DISCON);
            xSemaphoreGive(xSPI3Mutex);
            telnet_state = TELNET_STATE_LISTEN;
            stats.connections_active = 0;
            is_telnet_active = 0;
            break;
        }
        
        /* Check for inactivity timeout */
        if ((xTaskGetTickCount() - last_activity) > pdMS_TO_TICKS(TELNET_INACTIVITY_TIMEOUT / 1000)) {
            xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
            W5500_WriteByte(pw5500, W5500_Sn_CR, W5500_SOCKET_REG_BLOCK(W5500_SOCK_TELNET), W5500_Sn_CR_DISCON);
            xSemaphoreGive(xSPI3Mutex);
            telnet_state = TELNET_STATE_LISTEN;
            stats.connections_active = 0;
            stats.timeouts++;
            is_telnet_active = 0;
            break;
        }
        
        /* Check for received data */
        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
        rx_size = W5500_GetRxSize(pw5500, W5500_SOCK_TELNET);
        xSemaphoreGive(xSPI3Mutex);
        
        if (rx_size > 0) {
            last_activity = xTaskGetTickCount();
            
            if (rx_size > sizeof(rx_buffer)) {
                rx_size = sizeof(rx_buffer);
            }
            
            xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
            W5500_RecvData(pw5500, W5500_SOCK_TELNET, rx_buffer, rx_size);
            xSemaphoreGive(xSPI3Mutex);
            
            /* Process received characters */
            int echo_pos = 0;
            for (int i = 0; i < rx_size; i++) {
                uint8_t c = rx_buffer[i];
                
                /* Handle telnet IAC sequences */
                if (c == TELNET_IAC) {
                    i += 2;  /* Skip IAC and following bytes */
                    continue;
                }
                
                /* Handle printable characters */
                if (c >= 0x20 && c <= 0x7E) {
                    if (cmd_pos < (TELNET_MAX_LINE_LEN - 1)) {
                        cmd_line[cmd_pos++] = c;
                        tx_echo[echo_pos++] = c;  /* Echo back */
                    }
                }
                /* Handle backspace */
                else if (c == 0x08 || c == 0x7F) {
                    if (cmd_pos > 0) {
                        cmd_pos--;
                        tx_echo[echo_pos++] = 0x08;
                        tx_echo[echo_pos++] = 0x20;
                        tx_echo[echo_pos++] = 0x08;
                    }
                }
                /* Handle CR (Enter) */
                else if (c == 0x0D) {
                    tx_echo[echo_pos++] = 0x0D;
                    tx_echo[echo_pos++] = 0x0A;
                    
                    /* Echo the CR/LF */
                    if (echo_pos > 0) {
                        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
                        W5500_SendData(pw5500, W5500_SOCK_TELNET, tx_echo, echo_pos);
                        xSemaphoreGive(xSPI3Mutex);
                    }
                    
                    /* Process command */
                    cmd_line[cmd_pos] = '\0';
                    int result = CLI_ProcessCommand(&cli_ctx, cmd_line);
                    stats.commands_processed++;
                    
                    /* Handle exit/logout */
                    if (result == 1) {
                        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
                        W5500_WriteByte(pw5500, W5500_Sn_CR, W5500_SOCKET_REG_BLOCK(W5500_SOCK_TELNET), W5500_Sn_CR_DISCON);
                        xSemaphoreGive(xSPI3Mutex);
                        telnet_state = TELNET_STATE_LISTEN;
                        stats.connections_active = 0;
                        is_telnet_active = 0;
                    }
                    /* Handle reboot */
                    else if (result == 2) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                        NVIC_SystemReset();
                    }
                    else {
                        /* Send prompt for normal commands */
                        CLI_SendPrompt(&cli_ctx);
                    }
                    
                    cmd_pos = 0;
                    echo_pos = 0;
                }
                /* Handle Ctrl+C */
                else if (c == 0x03) {
                    cmd_pos = 0;
                    const char *msg = "^C\r\nready> ";
                    xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
                    W5500_SendData(pw5500, W5500_SOCK_TELNET, (uint8_t *)msg, strlen(msg));
                    xSemaphoreGive(xSPI3Mutex);
                    echo_pos = 0;
                }
            }
            
            /* Send any remaining echo */
            if (echo_pos > 0) {
                xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
                W5500_SendData(pw5500, W5500_SOCK_TELNET, tx_echo, echo_pos);
                xSemaphoreGive(xSPI3Mutex);
            }
        }
        break;
    }
}

/* Public Functions ----------------------------------------------------------*/

/**
 * @brief Initialize Telnet Task
 */
int TelnetTask_Init(W5500_Context_t *w5500_ctx) {
    /* Store W5500 context pointer */
    pw5500 = w5500_ctx;
    
    /* Reset statistics */
    memset(&stats, 0, sizeof(stats));
    
    /* Initialize CLI context */
    CLI_Init(&cli_ctx, Telnet_OutputFunc, NULL, response_buffer, sizeof(response_buffer));
    
    /* Initialize state */
    telnet_state = TELNET_STATE_CLOSED;
    last_activity = 0;
    
    return 0;
}

/**
 * @brief Telnet Task main function
 */
void vTelnetTask(void *argument) {
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(100);  /* 100ms period */
    
    /* Initialize the xLastWakeTime variable with the current time */
    xLastWakeTime = xTaskGetTickCount();
    
    /* Task main loop */
    for (;;) {
        /* Wait for the next cycle */
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        
        /* Process telnet connection */
        ProcessTelnetConnection();
    }
}

/**
 * @brief Get Telnet task statistics
 */
void TelnetTask_GetStats(TelnetStats_t *pstats) {
    if (pstats != NULL) {
        memcpy(pstats, &stats, sizeof(TelnetStats_t));
    }
}
