/**
 ******************************************************************************
 * @file    task_signaling.h
 * @brief   Signaling Protocol Task Header
 *          Handles client registration, keep-alive, disconnection protocol
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics / F4HDK NPR-70 Project
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 ******************************************************************************
 */

#ifndef TASK_SIGNALING_H
#define TASK_SIGNALING_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "si4463_driver.h"

/* Exported constants --------------------------------------------------------*/
#define SIGNALING_TASK_STACK_SIZE       (512)
#define SIGNALING_TASK_PRIORITY         (4)  // Medium priority
#define SIGNALING_TIMEOUT_MULTIPLIER    (10) // 10x signaling_period for timeout

/* Exported types ------------------------------------------------------------*/

/**
 * @brief Signaling task statistics
 */
typedef struct {
    uint32_t frames_processed;      // RX signaling frames processed
    uint32_t frames_sent;           // TX signaling frames sent
    uint32_t connect_requests;      // Connection requests (master/client)
    uint32_t connect_acks;          // Connection ACKs received/sent
    uint32_t connect_nacks;         // Connection NACKs received/sent
    uint32_t disconnect_requests;   // Disconnect requests
    uint32_t whois_broadcasts;      // WHOIS broadcasts sent
    uint32_t client_timeouts;       // Client timeouts detected (master)
    uint32_t master_timeouts;       // Master timeouts detected (client)
} SignalingStats_t;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize Signaling Task
 * @param si4463_ctx Pointer to SI4463 radio context
 * @retval 0 on success, -1 on error
 */
int SignalingTask_Init(SI4463_Context_t *si4463_ctx);

/**
 * @brief Signaling Task main function
 * @param argument: Not used
 */
void vSignalingTask(void *argument);

/**
 * @brief Get signaling task statistics
 * @param stats: Pointer to stats structure to fill
 */
void SignalingTask_GetStats(SignalingStats_t *stats);

/**
 * @brief Process received signaling frame (called from Radio Processing Task)
 * @param unFECdata: Pointer to decoded frame data
 * @param unFECsize: Size of decoded data
 * @param TA_input: Timing advance input
 */
void Signaling_ProcessRxFrame(uint8_t *unFECdata, int unFECsize, int TA_input);

/**
 * @brief Trigger immediate connect request (client mode)
 * @note Sets connection state to "waiting for connection"
 */
void Signaling_TriggerConnect(void);

/**
 * @brief Trigger disconnect request (client mode)
 * @note Sets connection state to "waiting for disconnection"
 */
void Signaling_TriggerDisconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_SIGNALING_H */
