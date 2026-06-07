/**
 ******************************************************************************
 * @file    task_signaling.c
 * @brief   Signaling Protocol Task Implementation
 *          Handles client registration, keep-alive, and disconnection
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics / F4HDK NPR-70 Project
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "task_signaling.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <string.h>
#include <stdio.h>

#include "app_common.h"
#include "fec_codec.h"
#include "si4463_driver.h"
#include "w5500_driver.h"
#include "task_tdma.h"

/* Private defines -----------------------------------------------------------*/
#define SIGNALING_PERIOD_MS         (CONF_signaling_period * 2000)  // 2x config period in ms
#define MAX_SIGNALING_FRAME_SIZE    (260)
#define MAX_RFRAME_SIZE             (380)
#define MAX_FEC_ENCODED_SIZE        (400)  /* FEC encoding overhead ~4/3, plus padding */

/* Private typedef -----------------------------------------------------------*/
/* Client connection states (used with my_client_radio_connexion_state):
 * 0 = Disconnected (not attempting connection)
 * 1 = Waiting (waiting for connection ACK)
 * 2 = Connected (connected to master)
 * 3 = Rejected (connection rejected by master)
 * 4 = Disconnecting (waiting for disconnect ACK)
 * 5 = Timeout (connection timed out)
 */

/* Private variables ---------------------------------------------------------*/
static TaskHandle_t xSignalingTaskHandle = NULL;
static SignalingStats_t stats = {0};
static SI4463_Context_t *hsi4463 = NULL;  /* SI4463 radio context */

// Signaling frame buffers
static uint8_t rframe_TX[MAX_RFRAME_SIZE];
static uint8_t TX_signal_frame_raw[MAX_SIGNALING_FRAME_SIZE];
static uint8_t TX_signal_frame_FEC[MAX_FEC_ENCODED_SIZE];  /* FEC encoded buffer */
static int TX_signal_frame_point = 0;

// Client state machine
// Note: my_client_radio_connexion_state is declared in app_common.h as uint8_t (volatile extern)
static int connect_state_machine_counter = 0;
static int time_counter_last_ack = 0;

// Temporary buffer for building signaling messages - moved to SRAM2 for consistency (60 bytes saved)
static uint8_t loc_data[60] PLACE_IN_SRAM2;

/* External variables --------------------------------------------------------*/
extern uint32_t last_rframe_seen;
extern SPI_HandleTypeDef hspi1;  // For SI4463
extern SPI_HandleTypeDef hspi3;  // For W5500

/* Private function prototypes -----------------------------------------------*/
static void Signaling_FrameExploitation(uint8_t *unFECdata, int unFECsize, int TA_input);
static void Signaling_WhoisInterpret(uint8_t loc_ID, uint8_t *loc_callsign,
                                     uint32_t loc_IP_start, uint32_t loc_IP_size,
                                     uint8_t RSSI_loc, uint16_t BER_loc, int16_t TA_loc);
static uint32_t Signaling_LookforIPRange(uint32_t req_size);
static void Signaling_ConnectReqProcess(uint8_t *client_callsign, uint32_t req_IP_size,
                                        uint8_t req_static_alloc, int TA_input);
static void Signaling_ConnectACKProcess(uint8_t *raw_data);
static void Signaling_ConnectNACKProcess(uint8_t reason_loc);
static void Signaling_ConnectReqTX(void);
static void Signaling_DisconnectReqProcess(uint8_t loc_ID, uint8_t *loc_callsign);
static void Signaling_DisconnectACKProcess(uint8_t loc_ID, uint8_t *loc_callsign);
static void Signaling_DisconnectReqTX(void);
static void Signaling_DisconnectACKTX(uint8_t loc_ID, uint8_t *loc_callsign);
static void Signaling_FrameInit(void);
static void Signaling_SingleWhoisTX(uint8_t loc_ID, char *loc_callsign,
                                    uint32_t loc_IP_start, uint32_t loc_IP_size,
                                    uint8_t RSSI_loc, uint16_t BER_loc, int16_t TA_loc);
static void Signaling_WhoisTX(void);
static void Signaling_TXAddEntry(uint8_t *raw_data, int size);
static void Signaling_FramePush(void);
static void Signaling_PeriodicCall(void);

/* Helper functions ----------------------------------------------------------*/

/**
 * @brief Convert 4-byte array to uint32_t IP address
 */
static uint32_t IP_char2int(uint8_t *ip_bytes) {
    return ((uint32_t)ip_bytes[0] << 24) |
           ((uint32_t)ip_bytes[1] << 16) |
           ((uint32_t)ip_bytes[2] << 8) |
           ((uint32_t)ip_bytes[3]);
}

/**
 * @brief Convert uint32_t IP address to 4-byte array
 */
static void IP_int2char(uint32_t ip, uint8_t *ip_bytes) {
    ip_bytes[0] = (ip >> 24) & 0xFF;
    ip_bytes[1] = (ip >> 16) & 0xFF;
    ip_bytes[2] = (ip >> 8) & 0xFF;
    ip_bytes[3] = ip & 0xFF;
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief Initialize Signaling Task
 */
int SignalingTask_Init(SI4463_Context_t *si4463_ctx) {
    /* Store SI4463 context */
    hsi4463 = si4463_ctx;
    
    // Reset statistics
    memset(&stats, 0, sizeof(stats));
    
    // Initialize state machine
    my_client_radio_connexion_state = 0;
    connect_state_machine_counter = 0;
    time_counter_last_ack = 0;
    TX_signal_frame_point = 0;
    
    return 0;
}

/**
 * @brief Signaling Task main function
 */
void vSignalingTask(void *argument) {
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(SIGNALING_PERIOD_MS);
    
    xLastWakeTime = xTaskGetTickCount();
    
    printf("[Signaling] Task started, period=%lu ms\r\n", SIGNALING_PERIOD_MS);
    
    for (;;) {
        // Wait for next period
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        
        // Call periodic signaling function
        Signaling_PeriodicCall();
    }
}

/**
 * @brief Get signaling task statistics
 */
void SignalingTask_GetStats(SignalingStats_t *pstats) {
    if (pstats != NULL) {
        taskENTER_CRITICAL();
        memcpy(pstats, &stats, sizeof(SignalingStats_t));
        taskEXIT_CRITICAL();
    }
}

/**
 * @brief Process received signaling frame (called from Radio Processing Task)
 */
void Signaling_ProcessRxFrame(uint8_t *unFECdata, int unFECsize, int TA_input) {
    Signaling_FrameExploitation(unFECdata, unFECsize, TA_input);
    
    taskENTER_CRITICAL();
    stats.frames_processed++;
    taskEXIT_CRITICAL();
}

/**
 * @brief Trigger connect request (client mode)
 */
void Signaling_TriggerConnect(void) {
    if (!is_TDMA_master) {
        taskENTER_CRITICAL();
        my_client_radio_connexion_state = 1;
        connect_state_machine_counter = 0;
        taskEXIT_CRITICAL();
    }
}

/**
 * @brief Trigger disconnect request (client mode)
 */
void Signaling_TriggerDisconnect(void) {
    if (!is_TDMA_master) {
        taskENTER_CRITICAL();
        my_client_radio_connexion_state = 4;
        connect_state_machine_counter = 0;
        taskEXIT_CRITICAL();
    }
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief Exploit received signaling frame
 */
static void Signaling_FrameExploitation(uint8_t *unFECdata, int unFECsize, int TA_input) {
    int data_pos = 2;
    uint8_t field_type;
    uint8_t field_length;
    uint8_t local_ID;
    uint8_t *local_callsign;
    uint32_t local_IP_start;
    uint32_t local_IP_size;
    uint8_t local_static_alloc;
    uint8_t local_reason;
    uint8_t local_RSSI;
    uint16_t local_BER;
    int16_t local_TA;
    
    do {
        field_type = unFECdata[data_pos];
        field_length = unFECdata[data_pos + 1];
        
        switch (field_type) {
            case 0x01:  // WHOIS
                local_ID = unFECdata[data_pos + 2];
                local_callsign = unFECdata + data_pos + 3;
                local_callsign[15] = 0;  // Force null termination
                local_IP_start = IP_char2int(unFECdata + data_pos + 19);
                local_IP_size = IP_char2int(unFECdata + data_pos + 23);
                local_RSSI = unFECdata[data_pos + 27];
                local_BER = unFECdata[data_pos + 28] + (unFECdata[data_pos + 29] << 8);
                local_TA = unFECdata[data_pos + 30] + (unFECdata[data_pos + 31] << 8);
                Signaling_WhoisInterpret(local_ID, local_callsign, local_IP_start,
                                        local_IP_size, local_RSSI, local_BER, local_TA);
                break;
                
            case 0x05:  // Request new connection
                local_callsign = unFECdata + data_pos + 2;
                local_callsign[15] = 0;
                local_IP_size = IP_char2int(unFECdata + data_pos + 18);
                local_static_alloc = unFECdata[data_pos + 22];
                if (is_TDMA_master) {
                    Signaling_ConnectReqProcess(local_callsign, local_IP_size,
                                               local_static_alloc, TA_input);
                    taskENTER_CRITICAL();
                    stats.connect_requests++;
                    taskEXIT_CRITICAL();
                }
                break;
                
            case 0x06:  // ACK new connection
                local_callsign = unFECdata + data_pos + 3;
                local_callsign[15] = 0;
                if ((!is_TDMA_master) &&
                    (strcmp((char *)local_callsign, CONF_radio_my_callsign) == 0)) {
                    Signaling_ConnectACKProcess(unFECdata + data_pos + 2);
                    taskENTER_CRITICAL();
                    stats.connect_acks++;
                    taskEXIT_CRITICAL();
                }
                break;
                
            case 0x07:  // NACK new connection
                local_callsign = unFECdata + data_pos + 2;
                local_callsign[15] = 0;
                local_reason = unFECdata[data_pos + 18];
                if ((!is_TDMA_master) &&
                    (strcmp((char *)local_callsign, CONF_radio_my_callsign) == 0)) {
                    Signaling_ConnectNACKProcess(local_reason);
                    taskENTER_CRITICAL();
                    stats.connect_nacks++;
                    taskEXIT_CRITICAL();
                }
                break;
                
            case 0x0B:  // Request disconnection
                local_ID = unFECdata[data_pos + 2];
                local_callsign = unFECdata + data_pos + 3;
                local_callsign[15] = 0;
                if (is_TDMA_master) {
                    Signaling_DisconnectReqProcess(local_ID, local_callsign);
                    taskENTER_CRITICAL();
                    stats.disconnect_requests++;
                    taskEXIT_CRITICAL();
                }
                break;
                
            case 0x0C:  // ACK disconnection
                local_ID = unFECdata[data_pos + 2];
                local_callsign = unFECdata + data_pos + 3;
                local_callsign[15] = 0;
                if ((!is_TDMA_master) &&
                    (strcmp((char *)local_callsign, CONF_radio_my_callsign) == 0)) {
                    Signaling_DisconnectACKProcess(local_ID, local_callsign);
                }
                break;
        }
        
        data_pos = data_pos + field_length + 2;
        
    } while ((field_type != 0xFF) && (data_pos < unFECsize));
}

/**
 * @brief Interpret WHOIS message
 */
static void Signaling_WhoisInterpret(uint8_t loc_ID, uint8_t *loc_callsign,
                                     uint32_t loc_IP_start, uint32_t loc_IP_size,
                                     uint8_t RSSI_loc, uint16_t BER_loc, int16_t TA_loc) {
    // Only useful for clients (slaves)
    if (!is_TDMA_master) {
        if (loc_ID == 0x7F) {  // Master entry
            strncpy(CONF_radio_master_callsign, (char *)loc_callsign, 15);
            CONF_radio_master_callsign[15] = 0;
        } else if (loc_ID < RADIO_ADDR_TABLE_SIZE) {
            uint32_t current_time = GetMicrosecondTimer();
            
            taskENTER_CRITICAL();
            CONF_radio_addr_table_date[loc_ID] = current_time;
            CONF_radio_addr_table_status[loc_ID] = 2;
            strncpy(CONF_radio_addr_table_callsign[loc_ID], (char *)loc_callsign, 15);
            CONF_radio_addr_table_callsign[loc_ID][15] = 0;
            CONF_radio_addr_table_IP_begin[loc_ID] = loc_IP_start;
            CONF_radio_addr_table_IP_size[loc_ID] = loc_IP_size;
            radio_addr_table_RSSI[loc_ID] = RSSI_loc;
            radio_addr_table_BER[loc_ID] = BER_loc;
            // TA handled by TDMA task
            taskEXIT_CRITICAL();
        }
    }
}

/**
 * @brief Look for available IP range
 */
static uint32_t Signaling_LookforIPRange(uint32_t req_size) {
    int i, j;
    uint32_t answer = 0xFFFFFFFF;
    uint32_t current_tested_pos;
    uint32_t next_alloc_IP;
    
    current_tested_pos = CONF_radio_IP_start;
    next_alloc_IP = CONF_radio_IP_start + CONF_radio_IP_size;
    
    // Look for next allocated IP
    for (j = 0; j < RADIO_ADDR_TABLE_SIZE; j++) {
        if ((CONF_radio_addr_table_status[j]) &&
            (CONF_radio_addr_table_IP_begin[j] >= current_tested_pos) &&
            (CONF_radio_addr_table_IP_begin[j] < next_alloc_IP)) {
            next_alloc_IP = CONF_radio_addr_table_IP_begin[j];
        }
    }
    
    if ((next_alloc_IP - current_tested_pos) >= req_size) {
        answer = current_tested_pos;
    }
    
    // Search through allocated ranges
    for (i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
        if (CONF_radio_addr_table_status[i]) {
            current_tested_pos = CONF_radio_addr_table_IP_begin[i] +
                                CONF_radio_addr_table_IP_size[i];
            if (current_tested_pos < answer) {
                next_alloc_IP = CONF_radio_IP_start + CONF_radio_IP_size;
                for (j = 0; j < RADIO_ADDR_TABLE_SIZE; j++) {
                    if ((CONF_radio_addr_table_status[j]) &&
                        (CONF_radio_addr_table_IP_begin[j] >= current_tested_pos) &&
                        (CONF_radio_addr_table_IP_begin[j] < next_alloc_IP)) {
                        next_alloc_IP = CONF_radio_addr_table_IP_begin[j];
                    }
                }
                if ((next_alloc_IP - current_tested_pos) >= req_size) {
                    answer = current_tested_pos;
                }
            }
        }
    }
    
    return answer;
}

/**
 * @brief Process connection request (master mode)
 */
static void Signaling_ConnectReqProcess(uint8_t *client_callsign, uint32_t req_IP_size,
                                        uint8_t req_static_alloc, int TA_input) {
    int loc_ack = 0;
    int i;
    int existing_entry = -1;
    uint8_t client_ID = 0xFF;
    uint8_t raw_answer[60];
    uint8_t NACK_reason = 0;
    uint32_t proposed_IP;
    uint8_t previous_status = 0;
    
    // Look for existing entry for this callsign
    for (i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
        if (strcmp((char *)client_callsign, CONF_radio_addr_table_callsign[i]) == 0) {
            existing_entry = i;
        }
    }
    
    if (existing_entry != -1) {
        previous_status = CONF_radio_addr_table_status[existing_entry];
    }
    
    // Case 1: Existing entry matches both callsign and size
    if ((existing_entry != -1) &&
        (req_IP_size == CONF_radio_addr_table_IP_size[existing_entry])) {
        client_ID = existing_entry;
        CONF_radio_addr_table_status[existing_entry] = 1;
        CONF_radio_addr_table_date[existing_entry] = GetMicrosecondTimer();
        loc_ack = 1;
    }
    
    // Case 2: Existing entry with different size
    if ((existing_entry != -1) &&
        (req_IP_size != CONF_radio_addr_table_IP_size[existing_entry])) {
        CONF_radio_addr_table_status[existing_entry] = 0;  // Free existing
        proposed_IP = Signaling_LookforIPRange(req_IP_size);
        if (proposed_IP != 0xFFFFFFFF) {
            client_ID = existing_entry;
            CONF_radio_addr_table_status[client_ID] = 1;
            CONF_radio_addr_table_IP_begin[client_ID] = proposed_IP;
            CONF_radio_addr_table_IP_size[client_ID] = req_IP_size;
            strncpy(CONF_radio_addr_table_callsign[client_ID], (char *)client_callsign, 15);
            CONF_radio_addr_table_callsign[client_ID][15] = 0;
            CONF_radio_addr_table_date[client_ID] = GetMicrosecondTimer();
            loc_ack = 1;
        } else {
            NACK_reason = 0x02;  // No more IP
            loc_ack = 0;
        }
    }
    
    // Initialize TA if newly connected
    if ((existing_entry != -1) && (previous_status == 0) && (loc_ack == 1)) {
        TDMA_init_TA(client_ID, TA_input);
    }
    
    // Case 3: No previous entry
    if (existing_entry == -1) {
        // Look for empty entry
        for (i = RADIO_ADDR_TABLE_SIZE - 1; i >= 0; i--) {
            if (CONF_radio_addr_table_status[i] <= 0) {
                client_ID = i;
            }
        }
        
        if (client_ID == 0xFF) {
            NACK_reason = 0x03;  // Max number of clients reached
            loc_ack = 0;
        } else {
            proposed_IP = Signaling_LookforIPRange(req_IP_size);
            if (proposed_IP != 0xFFFFFFFF) {
                CONF_radio_addr_table_status[client_ID] = 1;
                CONF_radio_addr_table_IP_begin[client_ID] = proposed_IP;
                CONF_radio_addr_table_IP_size[client_ID] = req_IP_size;
                strncpy(CONF_radio_addr_table_callsign[client_ID], (char *)client_callsign, 15);
                CONF_radio_addr_table_callsign[client_ID][15] = 0;
                CONF_radio_addr_table_date[client_ID] = GetMicrosecondTimer();
                TDMA_init_TA(client_ID, TA_input);
                loc_ack = 1;
            } else {
                NACK_reason = 0x02;  // No more IP
                loc_ack = 0;
            }
        }
    }
    
    // Send answer to client
    if (loc_ack == 1) {  // ACK
        raw_answer[0] = 0x06;  // Signaling type = connection acknowledge
        raw_answer[1] = 59;    // Size
        raw_answer[2] = client_ID;
        strncpy((char *)(raw_answer + 3), (char *)client_callsign, 16);
        IP_int2char(CONF_radio_addr_table_IP_begin[client_ID], raw_answer + 19);
        IP_int2char(CONF_radio_addr_table_IP_size[client_ID], raw_answer + 23);
        strncpy((char *)(raw_answer + 27), CONF_radio_my_callsign, 16);
        IP_int2char(LAN_conf_applied.LAN_modem_IP, raw_answer + 43);
        IP_int2char(LAN_conf_applied.LAN_subnet_mask, raw_answer + 47);
        raw_answer[51] = LAN_conf_applied.LAN_def_route_activ;
        IP_int2char(LAN_conf_applied.LAN_def_route, raw_answer + 52);
        raw_answer[56] = LAN_conf_applied.LAN_DNS_activ;
        IP_int2char(LAN_conf_applied.LAN_DNS_value, raw_answer + 57);
        Signaling_TXAddEntry(raw_answer, 61);
        Signaling_FramePush();
    } else {  // NACK
        raw_answer[0] = 0x07;  // Signaling type = connection NACK
        raw_answer[1] = 33;    // Size
        strncpy((char *)(raw_answer + 2), (char *)client_callsign, 16);
        raw_answer[18] = NACK_reason;
        strncpy((char *)(raw_answer + 19), CONF_radio_my_callsign, 16);
        Signaling_TXAddEntry(raw_answer, 35);
        Signaling_FramePush();
    }
}

/**
 * @brief Process connection ACK (client mode)
 */
static void Signaling_ConnectACKProcess(uint8_t *raw_data) {
    uint8_t local_client_ID;
    uint32_t local_IP_start;
    uint32_t local_IP_size;
    uint32_t local_modem_IP;
    uint32_t local_IP_subnet;
    uint8_t local_default_route_activ;
    uint32_t local_default_route;
    uint8_t local_DNS_activ;
    uint32_t local_DNS_value;
    int need_LAN_reset = 0;
    
    // Client ID
    local_client_ID = raw_data[0];
    my_radio_client_ID = local_client_ID;
    TDMA_NULL_frame_init(70);
    
    // IP Start
    local_IP_start = IP_char2int(raw_data + 17);
    if (local_IP_start != LAN_conf_applied.DHCP_range_start) {
        need_LAN_reset = 1;
    }
    LAN_conf_applied.DHCP_range_start = local_IP_start;
    
    // IP Size
    local_IP_size = IP_char2int(raw_data + 21);
    if (local_IP_size != LAN_conf_applied.DHCP_range_size) {
        need_LAN_reset = 1;
    }
    LAN_conf_applied.DHCP_range_size = local_IP_size;
    
    // Master Callsign
    strncpy(CONF_radio_master_callsign, (char *)(raw_data + 25), 15);
    CONF_radio_master_callsign[15] = 0;
    
    // Modem IP
    local_modem_IP = IP_char2int(raw_data + 41);
    if (local_modem_IP != LAN_conf_applied.LAN_modem_IP) {
        need_LAN_reset = 1;
    }
    LAN_conf_applied.LAN_modem_IP = local_modem_IP;
    
    // IP subnet mask
    local_IP_subnet = IP_char2int(raw_data + 45);
    if (local_IP_subnet != LAN_conf_applied.LAN_subnet_mask) {
        need_LAN_reset = 1;
    }
    LAN_conf_applied.LAN_subnet_mask = local_IP_subnet;
    
    // Default route active
    local_default_route_activ = raw_data[49];
    if (local_default_route_activ != LAN_conf_applied.LAN_def_route_activ) {
        need_LAN_reset = 1;
    }
    LAN_conf_applied.LAN_def_route_activ = local_default_route_activ;
    
    // Default route value
    local_default_route = IP_char2int(raw_data + 50);
    if (local_default_route != LAN_conf_applied.LAN_def_route) {
        need_LAN_reset = 1;
    }
    LAN_conf_applied.LAN_def_route = local_default_route;
    
    // DNS active
    local_DNS_activ = raw_data[54];
    if (local_DNS_activ != LAN_conf_applied.LAN_DNS_activ) {
        need_LAN_reset = 1;
    }
    LAN_conf_applied.LAN_DNS_activ = local_DNS_activ;
    
    // DNS value
    local_DNS_value = IP_char2int(raw_data + 55);
    if (local_DNS_value != LAN_conf_applied.LAN_DNS_value) {
        need_LAN_reset = 1;
    }
    LAN_conf_applied.LAN_DNS_value = local_DNS_value;
    
    // TODO: Implement LAN reset if needed
    // if (need_LAN_reset) {
    //     W5500_Reconfigure();
    //     DHCP_ResetTable();
    // }
    
    my_client_radio_connexion_state = 2;
    connect_state_machine_counter = 0;
    time_counter_last_ack = 0;
}

/**
 * @brief Process connection NACK (client mode)
 */
static void Signaling_ConnectNACKProcess(uint8_t reason_loc) {
    connect_rejection_reason = reason_loc;
    my_client_radio_connexion_state = 3;
    connect_state_machine_counter = 0;
}

/**
 * @brief Transmit connection request (client mode)
 */
static void Signaling_ConnectReqTX(void) {
    loc_data[0] = 0x05;  // Signaling type = connection request
    loc_data[1] = 21;    // Field size
    strncpy((char *)(loc_data + 2), CONF_radio_my_callsign, 16);
    IP_int2char(CONF_radio_IP_size_requested, loc_data + 18);
    loc_data[22] = CONF_radio_static_IP_requested;
    Signaling_TXAddEntry(loc_data, 23);
    Signaling_FramePush();
}

/**
 * @brief Process disconnect request (master mode)
 */
static void Signaling_DisconnectReqProcess(uint8_t loc_ID, uint8_t *loc_callsign) {
    int i;
    int existing_entry = -1;
    
    for (i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
        if (strcmp((char *)loc_callsign, CONF_radio_addr_table_callsign[i]) == 0) {
            existing_entry = i;
        }
    }
    
    if (existing_entry != -1) {
        CONF_radio_addr_table_status[existing_entry] = 0;
    }
    
    Signaling_DisconnectACKTX(loc_ID, loc_callsign);
    Signaling_DisconnectACKTX(loc_ID, loc_callsign);  // Send twice for reliability
}

/**
 * @brief Process disconnect ACK (client mode)
 */
static void Signaling_DisconnectACKProcess(uint8_t loc_ID, uint8_t *loc_callsign) {
    my_client_radio_connexion_state = 5;
    my_radio_client_ID = 0x7E;
    TDMA_NULL_frame_init(70);
}

/**
 * @brief Transmit disconnect request (client mode)
 */
static void Signaling_DisconnectReqTX(void) {
    loc_data[0] = 0x0B;  // Signaling type = disconnect request
    loc_data[1] = 17;    // Field size
    loc_data[2] = my_radio_client_ID;
    strncpy((char *)(loc_data + 3), CONF_radio_my_callsign, 16);
    Signaling_TXAddEntry(loc_data, 19);
    Signaling_FramePush();
}

/**
 * @brief Transmit disconnect ACK (master mode)
 */
static void Signaling_DisconnectACKTX(uint8_t loc_ID, uint8_t *loc_callsign) {
    loc_data[0] = 0x0C;  // Signaling type = disconnect ACK
    loc_data[1] = 17;    // Field size
    loc_data[2] = loc_ID;
    strncpy((char *)(loc_data + 3), (char *)loc_callsign, 16);
    Signaling_TXAddEntry(loc_data, 19);
    Signaling_FramePush();
}

/**
 * @brief Initialize signaling frame
 */
static void Signaling_FrameInit(void) {
    if (is_TDMA_master) {
        TX_signal_frame_raw[0] = 0xFF;  // Broadcast address (plus parity bit)
    } else {
        /* Add parity bit to client ID (7-bit value, bit 7 = even parity) */
        TX_signal_frame_raw[0] = my_radio_client_ID + parity_bit_elab[my_radio_client_ID & 0x7F];
    }
    TX_signal_frame_raw[1] = 0x1E;  // Protocol = signaling
    TX_signal_frame_point = 2;
}

/**
 * @brief Transmit single WHOIS entry
 */
static void Signaling_SingleWhoisTX(uint8_t loc_ID, char *loc_callsign,
                                    uint32_t loc_IP_start, uint32_t loc_IP_size,
                                    uint8_t RSSI_loc, uint16_t BER_loc, int16_t TA_loc) {
    loc_data[0] = 0x01;  // Field type = who
    loc_data[1] = 30;    // Field length
    loc_data[2] = loc_ID;
    
    strncpy((char *)(loc_data + 3), loc_callsign, 15);
    loc_data[18] = 0;  // Null termination of callsign
    IP_int2char(loc_IP_start, loc_data + 19);  // IP start
    IP_int2char(loc_IP_size, loc_data + 23);   // IP size
    loc_data[27] = RSSI_loc;                    // RSSI uplink
    loc_data[28] = BER_loc & 0xFF;             // Error rate uplink (LSB first)
    loc_data[29] = (BER_loc & 0xFF00) >> 8;
    loc_data[30] = TA_loc & 0xFF;
    loc_data[31] = (TA_loc & 0xFF00) >> 8;
    Signaling_TXAddEntry(loc_data, 32);
}

/**
 * @brief Transmit WHOIS broadcast
 */
static void Signaling_WhoisTX(void) {
    int i;
    uint8_t RSSI_loc;
    
    if (is_TDMA_master) {
        // Master entry
        Signaling_SingleWhoisTX(0x7F, CONF_radio_my_callsign,
                               LAN_conf_applied.LAN_modem_IP, 0, 0, 0, 0);
        
        // Client entries
        for (i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
            if (CONF_radio_addr_table_status[i]) {
                RSSI_loc = (radio_addr_table_RSSI[i] & 0xFF00) >> 8;
                Signaling_SingleWhoisTX(i, CONF_radio_addr_table_callsign[i],
                                       CONF_radio_addr_table_IP_begin[i],
                                       CONF_radio_addr_table_IP_size[i],
                                       RSSI_loc, radio_addr_table_BER[i],
                                       (int16_t)(TDMA_table_TA[i] / 10));
            }
        }
    } else if (my_client_radio_connexion_state == 2) {
        // Client's own entry
        Signaling_SingleWhoisTX(my_radio_client_ID, CONF_radio_my_callsign,
                               LAN_conf_applied.DHCP_range_start,
                               LAN_conf_applied.DHCP_range_size,
                               downlink_RSSI, downlink_BER, 0);
        // Master entry
        Signaling_SingleWhoisTX(0x7F, CONF_radio_master_callsign,
                               LAN_conf_applied.LAN_modem_IP, 0, 0, 0, 0);
    }
    
    taskENTER_CRITICAL();
    stats.whois_broadcasts++;
    taskEXIT_CRITICAL();
}

/**
 * @brief Add entry to TX signaling frame
 */
static void Signaling_TXAddEntry(uint8_t *raw_data, int size) {
    if (TX_signal_frame_point == 0) {
        Signaling_FrameInit();
    }
    
    if ((TX_signal_frame_point + size) < 248) {
        // Enough space for this entry in current frame
        memcpy(TX_signal_frame_raw + TX_signal_frame_point, raw_data, size);
        TX_signal_frame_point = TX_signal_frame_point + size;
    } else {
        // Not enough space, send previous entries
        Signaling_FramePush();
        Signaling_FrameInit();
        memcpy(TX_signal_frame_raw + TX_signal_frame_point, raw_data, size);
        TX_signal_frame_point = TX_signal_frame_point + size;
    }
}

/**
 * @brief Push signaling frame to TX FIFO
 */
static void Signaling_FramePush(void) {
    int size_wo_FEC;
    int size_w_FEC;
    uint8_t rframe_length;
    uint32_t timer_snapshot;
    HAL_StatusTypeDef status;
    
    /* Append END flag (0xFF) and size 0x00 */
    TX_signal_frame_raw[TX_signal_frame_point] = 0xFF;
    TX_signal_frame_raw[TX_signal_frame_point + 1] = 0x00;
    TX_signal_frame_point += 2;
    size_wo_FEC = TX_signal_frame_point;
    
    /* Minimum frame size for proper FEC encoding */
    if (size_wo_FEC < 69) {
        size_wo_FEC = 69;
    }
    
    /* Build frame header */
    timer_snapshot = GetMicrosecondTimer();
    rframe_TX[0] = (timer_snapshot >> 16) & 0xFF;  /* Timer high byte */
    
    /* FEC encode the data */
    size_w_FEC = FEC_Encode(TX_signal_frame_raw, TX_signal_frame_FEC, size_wo_FEC);
    
    /* Calculate packet length field for SI4463 (subtract offset) */
    rframe_length = (size_w_FEC + 1) - SI4463_OFFSET_SIZE;
    rframe_TX[1] = rframe_length;
    
    /* TDMA byte (0x00 for signaling frames) */
    rframe_TX[2] = 0x00;
    
    /* Copy FEC-encoded data to TX frame */
    memcpy(rframe_TX + 3, TX_signal_frame_FEC, size_w_FEC);
    
    /* Write to SI4463 TX FIFO */
    if (hsi4463 != NULL) {
        uint16_t total_size = size_w_FEC + 3;
        
        /* SI4463_WriteTxFifo accepts max 129 bytes at a time */
        if (total_size <= 129) {
            status = SI4463_WriteTxFifo(hsi4463, rframe_TX, total_size);
            if (status != HAL_OK) {
                printf("Signaling TX FIFO write error\r\n");
            }
        } else {
            /* Split into multiple writes if needed */
            uint16_t offset = 0;
            while (offset < total_size) {
                uint8_t chunk_size = (total_size - offset) > 129 ? 129 : (total_size - offset);
                status = SI4463_WriteTxFifo(hsi4463, rframe_TX + offset, chunk_size);
                if (status != HAL_OK) {
                    printf("Signaling TX FIFO write error (chunk)\r\n");
                    break;
                }
                offset += chunk_size;
            }
        }
        
        /* Increment sent frame counter */
        taskENTER_CRITICAL();
        stats.frames_sent++;
        taskEXIT_CRITICAL();
    }
    
    /* Reset frame pointer for next frame */
    TX_signal_frame_point = 0;
}

/**
 * @brief Periodic signaling function (called every 2-6 seconds)
 */
static void Signaling_PeriodicCall(void) {
    int i;
    uint32_t time_since_last_ack;
    uint32_t timer_snapshot;
    
    // CLIENT STATE MACHINE
    if (!is_TDMA_master) {
        if ((my_client_radio_connexion_state == 1) &&
            (connect_state_machine_counter > 2)) {
            // Waiting for connection
            Signaling_ConnectReqTX();
            connect_state_machine_counter = 0;
            
            // Check if need to trigger TX
            timer_snapshot = GetMicrosecondTimer();
            if ((timer_snapshot - last_rframe_seen) > CONF_radio_timeout_small) {
                // TODO: Trigger SI4463 TX preparation
            }
        }
        
        if ((my_client_radio_connexion_state == 2) &&
            (connect_state_machine_counter > 5)) {
            // Already connected, periodic update
            Signaling_ConnectReqTX();
            // No counter reset, the ACK reception does it
        }
        
        if ((my_client_radio_connexion_state == 3) &&
            (connect_state_machine_counter > 15)) {
            // Rejected, new attempt every 15 periods
            Signaling_ConnectReqTX();
            connect_state_machine_counter = 0;
        }
        
        if ((my_client_radio_connexion_state == 2) &&
            (time_counter_last_ack > SIGNALING_TIMEOUT_MULTIPLIER)) {
            // Timeout, no ACK received for long time
            my_client_radio_connexion_state = 1;
            // TODO: Flush TX FIFO
            my_radio_client_ID = 0x7E;
            TDMA_NULL_frame_init(70);
            
            taskENTER_CRITICAL();
            stats.master_timeouts++;
            taskEXIT_CRITICAL();
        }
        
        if ((my_client_radio_connexion_state == 4) &&
            (connect_state_machine_counter > 2)) {
            // Waiting for disconnection
            Signaling_DisconnectReqTX();
            connect_state_machine_counter = 0;
        }
        
        connect_state_machine_counter++;
        time_counter_last_ack++;
    }
    
    // MASTER: Timeout management for clients
    if (is_TDMA_master) {
        timer_snapshot = GetMicrosecondTimer();
        for (i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
            time_since_last_ack = timer_snapshot - CONF_radio_addr_table_date[i];
            if ((CONF_radio_addr_table_status[i] == 1) &&
                (time_since_last_ack > (2000000UL * SIGNALING_TIMEOUT_MULTIPLIER * CONF_signaling_period))) {
                CONF_radio_addr_table_status[i] = 0;  // Force disconnect
                
                taskENTER_CRITICAL();
                stats.client_timeouts++;
                taskEXIT_CRITICAL();
            }
        }
    }
    
    // Send WHOIS broadcast
    Signaling_WhoisTX();
    if (TX_signal_frame_point > 0) {
        Signaling_FramePush();
    }
}
