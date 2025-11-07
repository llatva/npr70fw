/*
  ******************************************************************************
  * @file    task_snmp.h
  * @brief   SNMP Agent Task for NPR-70
  ******************************************************************************
  * @attention
  *
  * Port of NPR-70 modem firmware from mbed OS to FreeRTOS
  * Original copyright (c) 2017-2020 Guillaume F. F4HDK
  * FreeRTOS port by Lasse OH3HZB
  *
  * SNMP v1 agent implementation - responds to GET/GET-NEXT/SET requests
  * Community: "public"
  * Port: UDP 161
  *
  ******************************************************************************
  */

#ifndef TASK_SNMP_H
#define TASK_SNMP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "w5500_driver.h"

/* Exported constants --------------------------------------------------------*/
#define SNMP_TASK_STACK_SIZE    (384)
#define SNMP_TASK_PRIORITY      (1)  /* Low priority service task */

/* SNMP Protocol Constants */
#define SNMP_COMMUNITY          "public"
#define SNMP_COMMUNITY_SIZE     6

/* ASN.1 Data types (RFC 1157) */
#define ASN_BOOLEAN             0x01
#define ASN_INTEGER             0x02
#define ASN_BITSTRING           0x03
#define ASN_OCTETSTRING         0x04
#define ASN_NULL                0x05
#define ASN_OBJECTID            0x06
#define ASN_SEQUENCE            0x10
#define ASN_SET                 0x11

#define SNMP_SEQUENCE           0x30
#define SNMP_SEQUENCE_OF        SNMP_SEQUENCE

#define ASN_APPLICATION         0x40
#define ASN_IPADDRESS           (ASN_APPLICATION | 0)
#define ASN_COUNTER             (ASN_APPLICATION | 1)
#define ASN_GAUGE               (ASN_APPLICATION | 2)
#define ASN_TIMETICKS           (ASN_APPLICATION | 3)
#define ASN_COUNTER64           (ASN_APPLICATION | 6)

/* SNMP Message Size Limits */
#define MAX_OID                 12
#define MAX_STRING              16
#define MAX_SNMPMSG_LEN         384

#define SNMP_V1                 0

/* SNMPv1 Commands */
#define GET_REQUEST             0xa0
#define GET_NEXT_REQUEST        0xa1
#define GET_RESPONSE            0xa2
#define SET_REQUEST             0xa3

/* SNMP Error Codes */
#define SNMP_SUCCESS            0
#define OID_NOT_FOUND          -1
#define TABLE_FULL             -2
#define ILLEGAL_LENGTH         -3
#define INVALID_ENTRY_ID       -4
#define INVALID_DATA_TYPE      -5

#define NO_SUCH_NAME            2
#define BAD_VALUE               3

/* Exported types ------------------------------------------------------------*/

/**
 * @brief SNMP task statistics
 */
typedef struct {
    uint32_t requests_received;
    uint32_t responses_sent;
    uint32_t parse_errors;
    uint32_t bad_community;
} SNMPStats_t;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize SNMP Task
 * @param w5500_ctx Pointer to W5500 driver context
 * @retval 0 on success, -1 on error
 */
int SNMPTask_Init(W5500_Context_t *w5500_ctx);

/**
 * @brief SNMP Task main function
 * @param argument: Not used
 */
void vSNMPTask(void *argument);

/**
 * @brief Get SNMP task statistics
 * @param stats: Pointer to stats structure to fill
 */
void SNMPTask_GetStats(SNMPStats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* TASK_SNMP_H */
