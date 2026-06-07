/*
  ******************************************************************************
  * @file    cli_commands.c
  * @brief   Common CLI command processing implementation
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "cli_commands.h"
#include "app_common.h"
#include "config_flash.h"
#include "task_radio_combined.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Private defines -----------------------------------------------------------*/
#define CMD_RESULT_NORMAL  0
#define CMD_RESULT_EXIT    1
#define CMD_RESULT_REBOOT  2

/* Public functions ----------------------------------------------------------*/

/**
 * @brief Initialize CLI context
 */
void CLI_Init(CLI_Context_t *ctx, CLI_OutputFunc_t output_func, 
              void *user_data, uint8_t *response_buffer, uint16_t buffer_size) {
    ctx->output_func = output_func;
    ctx->user_data = user_data;
    ctx->response_buffer = response_buffer;
    ctx->response_size = buffer_size;
}

/**
 * @brief Send welcome message
 */
void CLI_SendWelcome(CLI_Context_t *ctx) {
    const char *welcome = 
        "\r\n"
        "=====================================================\r\n"
        "  NPR-70 / TACNPR modem, FreeRTOS port v" FW_VERSION "\r\n"
        "  Type 'help' for commands\r\n"
        "=====================================================\r\n";
    
    if (ctx->output_func) {
        ctx->output_func((const uint8_t *)welcome, strlen(welcome), ctx->user_data);
    }
}

/**
 * @brief Send prompt
 */
void CLI_SendPrompt(CLI_Context_t *ctx) {
    const char *prompt = "ready> ";
    if (ctx->output_func) {
        ctx->output_func((const uint8_t *)prompt, strlen(prompt), ctx->user_data);
    }
}

/**
 * @brief Process CLI command
 */
int CLI_ProcessCommand(CLI_Context_t *ctx, const char *cmd) {
    uint8_t *tx_data = ctx->response_buffer;
    int len = 0;
    char cmd_str[24] = {0};
    char param1[24] = {0};
    char param2[24] = {0};
    
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
            "  exit, logout      - Close connection\r\n";
        strcpy((char *)tx_data, help_msg);
        len = strlen(help_msg);
    }
    /* Command: version */
    else if (strcmp(cmd_str, "version") == 0) {
        snprintf((char *)tx_data, ctx->response_size, 
                 "NPR-70 FreeRTOS Port\r\n"
                 "Firmware: %s\r\n"
                 "Build: %s %s\r\n"
                 "FreeRTOS: v11.1.0 LTS\r\n",
                 FW_VERSION, __DATE__, __TIME__);
        len = strlen((char *)tx_data);
    }
    /* Command: status */
    else if (strcmp(cmd_str, "status") == 0) {
        snprintf((char *)tx_data, ctx->response_size,
                 "Modem Status:\r\n"
                 "  Mode: %s\r\n"
                 "  Radio: %s\r\n"
                 "  Client ID: %u\r\n"
                 "  Connection: %s\r\n"
                 "  Uptime: %lu sec\r\n",
                 is_TDMA_master ? "Master" : "Client",
                 CONF_radio.state_ON_OFF ? "ON" : "OFF",
                 my_radio_client_ID,
                 my_client_radio_connexion_state ? "Connected" : "Disconnected",
                 xTaskGetTickCount() / 1000);
        len = strlen((char *)tx_data);
    }
    /* Command: show */
    else if (strcmp(cmd_str, "show") == 0 || strcmp(cmd_str, "display") == 0) {
        if (strcmp(param1, "config") == 0) {
            snprintf((char *)tx_data, ctx->response_size,
                     "Configuration:\r\n"
                     "  Network ID: %u\r\n"
                     "  Frequency: %u.%03u MHz\r\n"
                     "  Mode: %s\r\n"
                     "  Modulation: %u\r\n"
                     "  Radio State: %s\r\n",
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
            
            len = snprintf((char *)tx_data, ctx->response_size,
                          "FreeRTOS Tasks (%u):\r\n", (unsigned int)task_count);
            
            for (UBaseType_t i = 0; i < task_count && len < (ctx->response_size - 50); i++) {
                len += snprintf((char *)tx_data + len, ctx->response_size - len,
                               "  %-16s Pri:%u Stack:%u\r\n",
                               task_stats[i].pcTaskName,
                               (unsigned int)task_stats[i].uxCurrentPriority,
                               (unsigned int)task_stats[i].usStackHighWaterMark);
            }
        }
        else if (strcmp(param1, "memory") == 0) {
            size_t free_heap = xPortGetFreeHeapSize();
            size_t min_heap = xPortGetMinimumEverFreeHeapSize();
            
            snprintf((char *)tx_data, ctx->response_size,
                     "Memory Usage:\r\n"
                     "  Heap Free: %u bytes\r\n"
                     "  Heap Min:  %u bytes\r\n",
                     (unsigned int)free_heap, (unsigned int)min_heap);
            len = strlen((char *)tx_data);
        }
        else if (strcmp(param1, "buffers") == 0) {
            size_t free_heap = xPortGetFreeHeapSize();
            len = snprintf((char *)tx_data, ctx->response_size, "Buffers (LID: ptr, last_used_ms):\r\n");
            for (int i = 0; i < RADIO_ADDR_TABLE_SIZE && len < (ctx->response_size - 80); i++) {
                void *ptr = (void *)ethernet_buffer[i];
                uint32_t age = 0;
                if (buffer_last_used_ms[i] != 0) {
                    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    age = (now > buffer_last_used_ms[i]) ? (now - buffer_last_used_ms[i]) : 0;
                }
                len += snprintf((char *)tx_data + len, ctx->response_size - len,
                                "  [%d] %p  age=%lu ms\r\n", i, ptr, (unsigned long)age);
            }
            len += snprintf((char *)tx_data + len, ctx->response_size - len, 
                           "Heap free: %u bytes\r\n", (unsigned int)free_heap);
        }
        else if (strcmp(param1, "dhcp") == 0 || strcmp(param1, "DHCP_ARP") == 0) {
            len = snprintf((char *)tx_data, ctx->response_size,
                          "DHCP/ARP Entries:\r\n");
            
            int count = 0;
            for (int i = 0; i < RADIO_ADDR_TABLE_SIZE && len < (ctx->response_size - 80); i++) {
                if (CONF_radio_addr_table_status[i] != 0) {
                    uint32_t ip = CONF_radio_addr_table_IP_begin[i];
                    len += snprintf((char *)tx_data + len, ctx->response_size - len,
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
                len += snprintf((char *)tx_data + len, ctx->response_size - len,
                               "  (no entries)\r\n");
            }
        }
        else {
            strcpy((char *)tx_data, "Usage: show {config|tasks|memory|dhcp}\r\n");
            len = strlen((char *)tx_data);
        }
    }
    /* Command: radio */
    else if (strcmp(cmd_str, "radio") == 0) {
        if (strcmp(param1, "on") == 0) {
            CONF_radio.state_ON_OFF = 1;
            strcpy((char *)tx_data, "Radio is now ON.\r\n");
            len = strlen((char *)tx_data);
        }
        else if (strcmp(param1, "off") == 0) {
            CONF_radio.state_ON_OFF = 0;
            strcpy((char *)tx_data, "Radio is now OFF.\r\n");
            len = strlen((char *)tx_data);
        }
        else {
            strcpy((char *)tx_data, "Usage: radio {on|off}\r\n");
            len = strlen((char *)tx_data);
        }
    }
    /* Command: set */
    else if (strcmp(cmd_str, "set") == 0) {
        if (strlen(param1) == 0 || strlen(param2) == 0) {
            strcpy((char *)tx_data, "Usage: set <param> <value>\r\n");
            len = strlen((char *)tx_data);
        }
        else if (strcmp(param1, "network_id") == 0 || strcmp(param1, "radio_netw_ID") == 0) {
            int val = atoi(param2);
            if (val >= 0 && val <= 15) {
                CONF_radio_network_ID = (uint8_t)val;
                snprintf((char *)tx_data, ctx->response_size,
                         "Network ID set to %u\r\n", CONF_radio_network_ID);
                len = strlen((char *)tx_data);
            }
            else {
                strcpy((char *)tx_data, "ERROR: Network ID must be 0-15\r\n");
                len = strlen((char *)tx_data);
            }
        }
        else if (strcmp(param1, "frequency") == 0) {
            int freq_mhz = 0, freq_khz = 0;
            if (strchr(param2, '.')) {
                sscanf(param2, "%d.%d", &freq_mhz, &freq_khz);
                if (freq_mhz >= 420 && freq_mhz <= 450) {
                    CONF_frequency_HD = (uint16_t)((freq_mhz - 420) * 1000 + freq_khz);
                    snprintf((char *)tx_data, ctx->response_size,
                             "Frequency set to %u.%03u MHz\r\n",
                             420 + (CONF_frequency_HD / 1000),
                             CONF_frequency_HD % 1000);
                    len = strlen((char *)tx_data);
                }
                else {
                    strcpy((char *)tx_data, "ERROR: Frequency must be 420-450 MHz\r\n");
                    len = strlen((char *)tx_data);
                }
            }
            else {
                strcpy((char *)tx_data, "Usage: set frequency <MHz> (e.g. 437.000)\r\n");
                len = strlen((char *)tx_data);
            }
        }
        else if (strcmp(param1, "modulation") == 0) {
            int val = atoi(param2);
            if (((val >= 11) && (val <= 14)) || ((val >= 20) && (val <= 24))) {
                CONF_radio.modulation = (uint8_t)val;
                snprintf((char *)tx_data, ctx->response_size,
                         "Modulation set to %u\r\n", CONF_radio.modulation);
                len = strlen((char *)tx_data);
            }
            else {
                strcpy((char *)tx_data, "ERROR: Invalid modulation (11-14 or 20-24)\r\n");
                len = strlen((char *)tx_data);
            }
        }
        else if (strcmp(param1, "is_master") == 0) {
            if (strcmp(param2, "yes") == 0 || strcmp(param2, "1") == 0) {
                is_TDMA_master = 1;
                strcpy((char *)tx_data, "Master mode enabled\r\n");
                len = strlen((char *)tx_data);
            }
            else if (strcmp(param2, "no") == 0 || strcmp(param2, "0") == 0) {
                is_TDMA_master = 0;
                strcpy((char *)tx_data, "Client mode enabled\r\n");
                len = strlen((char *)tx_data);
            }
            else {
                strcpy((char *)tx_data, "Usage: set is_master {yes|no}\r\n");
                len = strlen((char *)tx_data);
            }
        }
        else if (strcmp(param1, "callsign") == 0) {
            if (strlen(param2) > 0 && strlen(param2) < 14) {
                strncpy(CONF_radio_my_callsign + 2, param2, 13);
                CONF_radio_my_callsign[15] = 0;
                snprintf((char *)tx_data, ctx->response_size,
                         "Callsign set to %s\r\n", CONF_radio_my_callsign + 2);
                len = strlen((char *)tx_data);
            }
            else {
                strcpy((char *)tx_data, "ERROR: Invalid callsign\r\n");
                len = strlen((char *)tx_data);
            }
        }
        else {
            snprintf((char *)tx_data, ctx->response_size,
                     "Unknown parameter: %s\r\n"
                     "Available: network_id, frequency, modulation, is_master, callsign\r\n",
                     param1);
            len = strlen((char *)tx_data);
        }
    }
    /* Command: save */
    else if (strcmp(cmd_str, "save") == 0) {
        HAL_StatusTypeDef status = Config_Flash_Save();
        
        if (status == HAL_OK) {
            strcpy((char *)tx_data, "Configuration saved to flash successfully.\r\n");
        } else {
            strcpy((char *)tx_data, "ERROR: Failed to save configuration to flash!\r\n");
        }
        len = strlen((char *)tx_data);
    }
    /* Command: who */
    else if (strcmp(cmd_str, "who") == 0) {
        len = snprintf((char *)tx_data, ctx->response_size,
                      "Master/Client Information:\r\n");
        
        if (is_TDMA_master) {
            len += snprintf((char *)tx_data + len, ctx->response_size - len,
                           "  Mode: MASTER\r\n");
            
            int clients = 0;
            for (int i = 0; i < RADIO_ADDR_TABLE_SIZE && len < (ctx->response_size - 50); i++) {
                if (CONF_radio_addr_table_status[i] != 0) {
                    len += snprintf((char *)tx_data + len, ctx->response_size - len,
                                   "  Client[%d]: %s\r\n",
                                   i, CONF_radio_addr_table_callsign[i]);
                    clients++;
                }
            }
            
            if (clients == 0) {
                len += snprintf((char *)tx_data + len, ctx->response_size - len,
                               "  (no clients connected)\r\n");
            }
        }
        else {
            len += snprintf((char *)tx_data + len, ctx->response_size - len,
                           "  Mode: CLIENT\r\n"
                           "  Client ID: %u\r\n"
                           "  Connection: %s\r\n"
                           "  Master: %s\r\n",
                           my_radio_client_ID,
                           my_client_radio_connexion_state ? "Connected" : "Disconnected",
                           CONF_radio_master_callsign);
        }
    }
    /* Command: reset_to_default */
    else if (strcmp(cmd_str, "reset_to_default") == 0) {
        const char *msg = "Restoring factory defaults and rebooting...\r\n";
        strcpy((char *)tx_data, msg);
        len = strlen(msg);
        
        /* Send response first */
        if (ctx->output_func && len > 0) {
            ctx->output_func(tx_data, len, ctx->user_data);
        }
        
        /* Restore factory defaults */
        Config_Flash_FactoryReset();
        
        vTaskDelay(pdMS_TO_TICKS(100));
        return CMD_RESULT_REBOOT;
    }
    /* Command: reboot */
    else if (strcmp(cmd_str, "reboot") == 0) {
        const char *msg = "Rebooting...\r\n";
        strcpy((char *)tx_data, msg);
        len = strlen(msg);
        
        /* Send response first */
        if (ctx->output_func && len > 0) {
            ctx->output_func(tx_data, len, ctx->user_data);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
        return CMD_RESULT_REBOOT;
    }
    /* Command: exit/logout */
    else if (strcmp(cmd_str, "exit") == 0 || strcmp(cmd_str, "logout") == 0) {
        const char *msg = "Goodbye!\r\n";
        strcpy((char *)tx_data, msg);
        len = strlen(msg);
        
        if (ctx->output_func && len > 0) {
            ctx->output_func(tx_data, len, ctx->user_data);
        }
        
        return CMD_RESULT_EXIT;
    }
    /* Command: 73 */
    else if (strcmp(cmd_str, "73") == 0) {
        strcpy((char *)tx_data, "73!\r\n");
        len = strlen((char *)tx_data);
    }
    /* Unknown command or empty line */
    else if (strlen(cmd_str) > 0) {
        snprintf((char *)tx_data, ctx->response_size, 
                 "Unknown command: %s\r\nType 'help' for commands\r\n", cmd_str);
        len = strlen((char *)tx_data);
    }
    else {
        /* Empty line - just send prompt */
        len = 0;
    }
    
    /* Send response if any */
    if (ctx->output_func && len > 0) {
        ctx->output_func(tx_data, len, ctx->user_data);
    }
    
    return CMD_RESULT_NORMAL;
}
