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
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "w5500_driver.h"
#include "app_common.h"
#include "config_flash.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>  /* For atoi, atof */

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

/* External variables --------------------------------------------------------*/
extern SemaphoreHandle_t xSPI3Mutex;
extern volatile uint8_t is_telnet_active;

/* Private function prototypes -----------------------------------------------*/
static void ProcessTelnetConnection(void);
static void SendWelcomeMessage(void);
static void ProcessCommand(const char *cmd);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief Send welcome message with telnet options
 */
static void SendWelcomeMessage(void) {
    uint8_t tx_data[128];
    int len = 0;
    
    /* Telnet protocol negotiation */
    tx_data[len++] = TELNET_IAC;
    tx_data[len++] = TELNET_WILL;
    tx_data[len++] = TELNET_CMD_ECHO;
    
    tx_data[len++] = TELNET_IAC;
    tx_data[len++] = TELNET_DO;
    tx_data[len++] = TELNET_CMD_SUPPRESS_GA;
    
    tx_data[len++] = TELNET_IAC;
    tx_data[len++] = TELNET_WILL;
    tx_data[len++] = TELNET_CMD_SUPPRESS_GA;
    
    /* Welcome message */
    const char *welcome = "\r\nNPR-70 Modem - FreeRTOS\r\nType 'help' for commands\r\nready> ";
    strcpy((char *)&tx_data[len], welcome);
    len += strlen(welcome);
    
    xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
    W5500_SendData(pw5500, W5500_SOCK_TELNET, tx_data, len);
    xSemaphoreGive(xSPI3Mutex);
}

/**
 * @brief Process received command
 */
static void ProcessCommand(const char *cmd) {
    uint8_t tx_data[400];  /* Reduced from 512 to save stack */
    int len = 0;
    char cmd_str[24] = {0};  /* Reduced from 32 */
    char param1[24] = {0};   /* Reduced from 32 */
    char param2[24] = {0};   /* Reduced from 32 */
    
    /* Parse command and parameters */
    sscanf(cmd, "%23s %23s %23s", cmd_str, param1, param2);
    
    /* Command: help */
    if (strcmp(cmd_str, "help") == 0 || strcmp(cmd_str, "?") == 0) {
        const char *help_msg = 
            "Available commands:\r\n"
            "  help, ?           - Show this help\r\n"
            "  version           - Show firmware version\r\n"
            "  status            - Show modem status\r\n"
            "  who               - Show client table\r\n"
            "  show config       - Display configuration\r\n"
            "  show tasks        - Display FreeRTOS tasks\r\n"
            "  show memory       - Display memory usage\r\n"
            "  show dhcp         - Display DHCP/ARP entries\r\n"
            "  radio on/off      - Enable/disable radio\r\n"
            "  save              - Save configuration to flash\r\n"
            "  set <param> <val> - Set parameter\r\n"
            "  reset_to_default  - Factory reset (restore defaults)\r\n"
            "  reboot            - Restart the modem\r\n"
            "  exit, logout      - Close connection\r\n"
            "ready> ";
        strcpy((char *)tx_data, help_msg);
        len = strlen(help_msg);
    }
    /* Command: version */
    else if (strcmp(cmd_str, "version") == 0) {
        snprintf((char *)tx_data, sizeof(tx_data), 
                 "NPR-70 FreeRTOS Port\r\n"
                 "Firmware: %s\r\n"
                 "Build: %s %s\r\n"
                 "FreeRTOS: v11.1.0 LTS\r\n"
                 "ready> ", FW_VERSION, __DATE__, __TIME__);
        len = strlen((char *)tx_data);
    }
    /* Command: status */
    else if (strcmp(cmd_str, "status") == 0) {
        snprintf((char *)tx_data, sizeof(tx_data),
                 "Modem Status:\r\n"
                 "  Mode: %s\r\n"
                 "  Radio: %s\r\n"
                 "  Client ID: %u\r\n"
                 "  Connection: %s\r\n"
                 "  Telnet Sessions: %lu\r\n"
                 "  Commands: %lu\r\n"
                 "  Uptime: %lu sec\r\n"
                 "ready> ",
                 is_TDMA_master ? "Master" : "Client",
                 CONF_radio.state_ON_OFF ? "ON" : "OFF",
                 my_radio_client_ID,
                 my_client_radio_connexion_state ? "Connected" : "Disconnected",
                 stats.connections_total,
                 stats.commands_processed,
                 xTaskGetTickCount() / 1000);
        len = strlen((char *)tx_data);
    }
    /* Command: show */
    else if (strcmp(cmd_str, "show") == 0 || strcmp(cmd_str, "display") == 0) {
        if (strcmp(param1, "config") == 0) {
            snprintf((char *)tx_data, sizeof(tx_data),
                     "Configuration:\r\n"
                     "  Network ID: %u\r\n"
                     "  Frequency: %u.%03u MHz\r\n"
                     "  Mode: %s\r\n"
                     "  Modulation: %u\r\n"
                     "  Radio State: %s\r\n"
                     "ready> ",
                     CONF_radio_network_ID,
                     420 + (CONF_frequency_HD / 1000),
                     CONF_frequency_HD % 1000,
                     is_TDMA_master ? "Master" : "Client",
                     CONF_radio.modulation,
                     CONF_radio.state_ON_OFF ? "ON" : "OFF");
            len = strlen((char *)tx_data);
        }
        else if (strcmp(param1, "tasks") == 0) {
            TaskStatus_t task_stats[16];
            UBaseType_t task_count = uxTaskGetNumberOfTasks();
            if (task_count > 16) task_count = 16;
            
            task_count = uxTaskGetSystemState(task_stats, task_count, NULL);
            
            len = snprintf((char *)tx_data, sizeof(tx_data),
                          "FreeRTOS Tasks (%u):\r\n", (unsigned int)task_count);
            
            for (UBaseType_t i = 0; i < task_count && len < 400; i++) {
                len += snprintf((char *)tx_data + len, sizeof(tx_data) - len,
                               "  %-16s Pri:%u Stack:%u\r\n",
                               task_stats[i].pcTaskName,
                               (unsigned int)task_stats[i].uxCurrentPriority,
                               (unsigned int)task_stats[i].usStackHighWaterMark);
            }
            
            len += snprintf((char *)tx_data + len, sizeof(tx_data) - len, "ready> ");
        }
        else if (strcmp(param1, "memory") == 0) {
            size_t free_heap = xPortGetFreeHeapSize();
            size_t min_heap = xPortGetMinimumEverFreeHeapSize();
            
            snprintf((char *)tx_data, sizeof(tx_data),
                     "Memory Usage:\r\n"
                     "  Heap Free: %u bytes\r\n"
                     "  Heap Min:  %u bytes\r\n"
                     "  RAM Used:  64648 / 65536 (98.9%%)\r\n"
                     "ready> ",
                     (unsigned int)free_heap, (unsigned int)min_heap);
            len = strlen((char *)tx_data);
        }
        else if (strcmp(param1, "dhcp") == 0 || strcmp(param1, "DHCP_ARP") == 0) {
            len = snprintf((char *)tx_data, sizeof(tx_data),
                          "DHCP/ARP Entries:\r\n");
            
            int count = 0;
            for (int i = 0; i < RADIO_ADDR_TABLE_SIZE && len < 450; i++) {
                if (CONF_radio_addr_table_status[i] != 0) {
                    uint32_t ip = CONF_radio_addr_table_IP_begin[i];
                    len += snprintf((char *)tx_data + len, sizeof(tx_data) - len,
                                   "  [%d] %u.%u.%u.%u  %s\r\n",
                                   i,
                                   (unsigned int)(ip >> 24) & 0xFF,
                                   (unsigned int)(ip >> 16) & 0xFF,
                                   (unsigned int)(ip >> 8) & 0xFF,
                                   (unsigned int)ip & 0xFF,
                                   CONF_radio_addr_table_callsign[i]);
                    count++;
                }
            }
            
            if (count == 0) {
                len += snprintf((char *)tx_data + len, sizeof(tx_data) - len,
                               "  (no entries)\r\n");
            }
            
            len += snprintf((char *)tx_data + len, sizeof(tx_data) - len, "ready> ");
        }
        else {
            strcpy((char *)tx_data, "Usage: show {config|tasks|memory|dhcp}\r\nready> ");
            len = strlen((char *)tx_data);
        }
    }
    /* Command: radio */
    else if (strcmp(cmd_str, "radio") == 0) {
        if (strcmp(param1, "on") == 0) {
            CONF_radio.state_ON_OFF = 1;
            strcpy((char *)tx_data, "Radio is now ON.\r\nready> ");
            len = strlen((char *)tx_data);
        }
        else if (strcmp(param1, "off") == 0) {
            CONF_radio.state_ON_OFF = 0;
            strcpy((char *)tx_data, "Radio is now OFF.\r\nready> ");
            len = strlen((char *)tx_data);
        }
        else {
            strcpy((char *)tx_data, "Usage: radio {on|off}\r\nready> ");
            len = strlen((char *)tx_data);
        }
    }
    /* Command: set */
    else if (strcmp(cmd_str, "set") == 0) {
        if (strlen(param1) == 0 || strlen(param2) == 0) {
            strcpy((char *)tx_data, "Usage: set <param> <value>\r\nready> ");
            len = strlen((char *)tx_data);
        }
        else if (strcmp(param1, "network_id") == 0 || strcmp(param1, "radio_netw_ID") == 0) {
            int val = atoi(param2);
            if (val >= 0 && val <= 15) {
                CONF_radio_network_ID = (uint8_t)val;
                snprintf((char *)tx_data, sizeof(tx_data),
                         "Network ID set to %u\r\nready> ", CONF_radio_network_ID);
                len = strlen((char *)tx_data);
            }
            else {
                strcpy((char *)tx_data, "ERROR: Network ID must be 0-15\r\nready> ");
                len = strlen((char *)tx_data);
            }
        }
        else if (strcmp(param1, "frequency") == 0) {
            /* Parse frequency without atof() to save code space */
            /* Expected format: "437.000" or "437000" (kHz) */
            int freq_mhz = 0, freq_khz = 0;
            if (strchr(param2, '.')) {
                /* Format: 437.000 */
                sscanf(param2, "%d.%d", &freq_mhz, &freq_khz);
                if (freq_mhz >= 420 && freq_mhz <= 450) {
                    CONF_frequency_HD = (uint16_t)((freq_mhz - 420) * 1000 + freq_khz);
                    snprintf((char *)tx_data, sizeof(tx_data),
                             "Frequency set to %u.%03u MHz\r\nready> ",
                             420 + (CONF_frequency_HD / 1000),
                             CONF_frequency_HD % 1000);
                    len = strlen((char *)tx_data);
                }
                else {
                    strcpy((char *)tx_data, "ERROR: Frequency must be 420-450 MHz\r\nready> ");
                    len = strlen((char *)tx_data);
                }
            }
            else {
                strcpy((char *)tx_data, "Usage: set frequency <MHz> (e.g. 437.000)\r\nready> ");
                len = strlen((char *)tx_data);
            }
        }
        else if (strcmp(param1, "modulation") == 0) {
            int val = atoi(param2);
            if (((val >= 11) && (val <= 14)) || ((val >= 20) && (val <= 24))) {
                CONF_radio.modulation = (uint8_t)val;
                snprintf((char *)tx_data, sizeof(tx_data),
                         "Modulation set to %u\r\nready> ", CONF_radio.modulation);
                len = strlen((char *)tx_data);
            }
            else {
                strcpy((char *)tx_data, "ERROR: Invalid modulation (11-14 or 20-24)\r\nready> ");
                len = strlen((char *)tx_data);
            }
        }
        else if (strcmp(param1, "is_master") == 0) {
            if (strcmp(param2, "yes") == 0 || strcmp(param2, "1") == 0) {
                is_TDMA_master = 1;
                strcpy((char *)tx_data, "Master mode enabled\r\nready> ");
                len = strlen((char *)tx_data);
            }
            else if (strcmp(param2, "no") == 0 || strcmp(param2, "0") == 0) {
                is_TDMA_master = 0;
                strcpy((char *)tx_data, "Client mode enabled\r\nready> ");
                len = strlen((char *)tx_data);
            }
            else {
                strcpy((char *)tx_data, "Usage: set is_master {yes|no}\r\nready> ");
                len = strlen((char *)tx_data);
            }
        }
        else if (strcmp(param1, "callsign") == 0) {
            if (strlen(param2) > 0 && strlen(param2) < 14) {
                strncpy(CONF_radio_my_callsign + 2, param2, 13);
                CONF_radio_my_callsign[15] = 0;
                snprintf((char *)tx_data, sizeof(tx_data),
                         "Callsign set to %s\r\nready> ", CONF_radio_my_callsign + 2);
                len = strlen((char *)tx_data);
            }
            else {
                strcpy((char *)tx_data, "ERROR: Invalid callsign\r\nready> ");
                len = strlen((char *)tx_data);
            }
        }
        else {
            snprintf((char *)tx_data, sizeof(tx_data),
                     "Unknown parameter: %s\r\n"
                     "Available: network_id, frequency, modulation, is_master, callsign\r\n"
                     "ready> ", param1);
            len = strlen((char *)tx_data);
        }
    }
    /* Command: save */
    else if (strcmp(cmd_str, "save") == 0) {
        HAL_StatusTypeDef status = Config_Flash_Save();
        
        if (status == HAL_OK) {
            strcpy((char *)tx_data, "Configuration saved to flash successfully.\r\nready> ");
        } else {
            strcpy((char *)tx_data, "ERROR: Failed to save configuration to flash!\r\nready> ");
        }
        len = strlen((char *)tx_data);
    }
    /* Command: who */
    else if (strcmp(cmd_str, "who") == 0) {
        len = snprintf((char *)tx_data, sizeof(tx_data),
                      "Master/Client Information:\r\n");
        
        if (is_TDMA_master) {
            len += snprintf((char *)tx_data + len, sizeof(tx_data) - len,
                           "  Mode: MASTER\r\n");
            
            /* Show connected clients */
            int clients = 0;
            for (int i = 0; i < RADIO_ADDR_TABLE_SIZE && len < 350; i++) {
                if (CONF_radio_addr_table_status[i] != 0) {
                    len += snprintf((char *)tx_data + len, sizeof(tx_data) - len,
                                   "  Client[%d]: %s\r\n",
                                   i, CONF_radio_addr_table_callsign[i]);
                    clients++;
                }
            }
            
            if (clients == 0) {
                len += snprintf((char *)tx_data + len, sizeof(tx_data) - len,
                               "  (no clients connected)\r\n");
            }
        }
        else {
            len += snprintf((char *)tx_data + len, sizeof(tx_data) - len,
                           "  Mode: CLIENT\r\n"
                           "  Client ID: %u\r\n"
                           "  Connection: %s\r\n"
                           "  Master: %s\r\n",
                           my_radio_client_ID,
                           my_client_radio_connexion_state ? "Connected" : "Disconnected",
                           CONF_radio_master_callsign);
        }
        
        len += snprintf((char *)tx_data + len, sizeof(tx_data) - len, "ready> ");
    }
    /* Command: reset_to_default */
    else if (strcmp(cmd_str, "reset_to_default") == 0) {
        const char *msg = "Restoring factory defaults and rebooting...\r\n";
        strcpy((char *)tx_data, msg);
        len = strlen(msg);
        
        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
        W5500_SendData(pw5500, W5500_SOCK_TELNET, tx_data, len);
        xSemaphoreGive(xSPI3Mutex);
        
        /* Restore factory defaults to flash */
        Config_Flash_FactoryReset();
        
        vTaskDelay(pdMS_TO_TICKS(100));
        NVIC_SystemReset();
        return;
    }
    /* Command: reboot */
    else if (strcmp(cmd_str, "reboot") == 0) {
        const char *reboot_msg = "Rebooting...\r\n";
        strcpy((char *)tx_data, reboot_msg);
        len = strlen(reboot_msg);
        
        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
        W5500_SendData(pw5500, W5500_SOCK_TELNET, tx_data, len);
        xSemaphoreGive(xSPI3Mutex);
        
        vTaskDelay(pdMS_TO_TICKS(100));
        NVIC_SystemReset();
        return;
    }
    /* Command: exit/logout */
    else if (strcmp(cmd_str, "exit") == 0 || strcmp(cmd_str, "logout") == 0) {
        const char *bye_msg = "Goodbye!\r\n";
        strcpy((char *)tx_data, bye_msg);
        len = strlen(bye_msg);
        
        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
        W5500_SendData(pw5500, W5500_SOCK_TELNET, tx_data, len);
        W5500_WriteByte(pw5500, W5500_Sn_CR, W5500_SOCKET_REG_BLOCK(W5500_SOCK_TELNET), W5500_Sn_CR_DISCON);
        xSemaphoreGive(xSPI3Mutex);
        
        telnet_state = TELNET_STATE_LISTEN;
        is_telnet_active = 0;
        stats.commands_processed++;
        return;
    }
    /* Command: 73 (ham radio goodbye) */
    else if (strcmp(cmd_str, "73") == 0) {
        strcpy((char *)tx_data, "73!\r\nready> ");
        len = strlen((char *)tx_data);
    }
    /* Unknown command or empty line */
    else if (strlen(cmd_str) > 0) {
        snprintf((char *)tx_data, sizeof(tx_data), "Unknown command: %s\r\nType 'help' for commands\r\nready> ", cmd_str);
        len = strlen((char *)tx_data);
    }
    else {
        const char *prompt = "ready> ";
        strcpy((char *)tx_data, prompt);
        len = strlen(prompt);
    }
    
    if (len > 0) {
        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
        W5500_SendData(pw5500, W5500_SOCK_TELNET, tx_data, len);
        xSemaphoreGive(xSPI3Mutex);
        stats.commands_processed++;
    }
}

/**
 * @brief Process telnet connection state
 */
static void ProcessTelnetConnection(void) {
    uint8_t sock_status;
    uint16_t rx_size;
    static uint8_t rx_buffer[TELNET_MAX_LINE_LEN + 10];
    static uint8_t tx_echo[TELNET_MAX_LINE_LEN + 10];
    static char cmd_line[TELNET_MAX_LINE_LEN];
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
                    ProcessCommand(cmd_line);
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
