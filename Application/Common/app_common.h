/*
  ******************************************************************************
  * @file    app_common.h
  * @brief   Common definitions for NPR-70 application
  ******************************************************************************
  * @attention
  *
  * Port of NPR-70 modem firmware from mbed OS to FreeRTOS
  * Original copyright (c) 2017-2020 Guillaume F. F4HDK
  * FreeRTOS port by Lasse OH3HZB
  *
  ******************************************************************************
  */

#ifndef APP_COMMON_H
#define APP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>
#include <string.h>

/* Exported defines ----------------------------------------------------------*/
#define FW_VERSION "2025_11_07-freertos-v1.0"

/* Radio configuration constants - from si4463_driver.h */
#define RADIO_ADDR_TABLE_SIZE 16

/* RX FIFO configuration */
#define RX_FIFO_SIZE 0x2000  /* 8KB circular buffer */
#define RX_FIFO_MASK (RX_FIFO_SIZE - 1)

/* Queue sizes */
#define RADIO_ISR_QUEUE_SIZE 8
#define RADIO_TX_QUEUE_SIZE 16
#define ETHERNET_RX_QUEUE_SIZE 8
#define ETHERNET_TX_QUEUE_SIZE 8

/* Exported types ------------------------------------------------------------*/

/**
 * @brief Radio interrupt event structure
 */
typedef struct {
    uint8_t event_type;     /* 0=RX, 1=TX */
    uint32_t timestamp;     /* Microsecond timestamp from TIM2 */
} RadioISREvent_t;

/**
 * @brief Radio RX packet structure
 */
typedef struct {
    uint32_t timestamp;     /* Reception timestamp */
    uint8_t rssi;           /* RSSI value */
    uint16_t length;        /* Packet length */
    uint8_t data[384];      /* Packet data */
} RadioRxPacket_t;

/**
 * @brief Ethernet packet structure
 */
typedef struct {
    uint16_t socket;        /* Socket number */
    uint16_t length;        /* Packet length */
    uint8_t data[1600];     /* Packet data (MTU) */
} EthernetPacket_t;

/**
 * @brief LAN configuration structure
 */
typedef struct {
    uint32_t LAN_modem_IP;
    uint32_t DHCP_range_start;
    uint32_t DHCP_range_size;
    uint32_t LAN_subnet_mask;
    uint32_t LAN_def_route;
    uint8_t LAN_def_route_activ;
    uint8_t LAN_DNS_activ;
    uint32_t LAN_DNS_value;
    uint8_t DHCP_server_active;
} LAN_conf_T;

/**
 * @brief Radio configuration structure
 */
typedef struct {
    uint8_t modulation;             /* 10, 11, 12, 20, 21, 22 */
    uint8_t default_state_ON_OFF;   /* Radio on/off at startup */
    uint8_t state_ON_OFF;           /* Current radio state */
    uint8_t master_FDD;             /* 0=TDD, 1=FDD_down, 2=FDD_up */
    uint16_t long_preamble_duration_for_TA;
    uint8_t addr_table_status[RADIO_ADDR_TABLE_SIZE];
    uint32_t addr_table_IP_begin[RADIO_ADDR_TABLE_SIZE];
} RadioConfig_t;

/* Exported variables --------------------------------------------------------*/

/* FreeRTOS handles - defined in main.c */
extern QueueHandle_t xRadioISRQueue;
extern QueueHandle_t xRadioTxQueue;
extern QueueHandle_t xEthernetRxQueue;
extern QueueHandle_t xEthernetTxQueue;

extern SemaphoreHandle_t xSPI1Mutex;
extern SemaphoreHandle_t xSPI3Mutex;
extern SemaphoreHandle_t xConfigMutex;

/* Global configuration */
extern LAN_conf_T LAN_conf_applied;
extern RadioConfig_t CONF_radio;

/* Radio address table (separate arrays for compatibility) */
extern char CONF_radio_my_callsign[16];
extern char CONF_radio_master_callsign[16];
extern uint32_t CONF_radio_addr_table_IP_begin[RADIO_ADDR_TABLE_SIZE];
extern uint32_t CONF_radio_addr_table_IP_size[RADIO_ADDR_TABLE_SIZE];
extern char CONF_radio_addr_table_callsign[RADIO_ADDR_TABLE_SIZE][16];
extern uint8_t CONF_radio_addr_table_status[RADIO_ADDR_TABLE_SIZE];
extern uint32_t CONF_radio_addr_table_date[RADIO_ADDR_TABLE_SIZE];
extern uint32_t CONF_radio_IP_start;
extern uint32_t CONF_radio_IP_size;
extern uint32_t CONF_radio_IP_size_requested;
extern uint8_t CONF_radio_static_IP_requested;

/* Radio signal quality arrays */
extern volatile uint16_t radio_addr_table_RSSI[RADIO_ADDR_TABLE_SIZE];
extern volatile uint16_t radio_addr_table_BER[RADIO_ADDR_TABLE_SIZE];

/* Global state variables */
extern volatile uint8_t is_TDMA_master;
extern volatile uint8_t is_SRAM_ext;
extern volatile uint8_t is_telnet_active;
extern volatile uint8_t my_client_radio_connexion_state;
extern uint8_t my_radio_client_ID;  /* This client's radio address (0-15) */

/* RX FIFO (circular buffer for radio reception) */
extern uint8_t RX_FIFO_data[RX_FIFO_SIZE];
extern volatile uint16_t RX_FIFO_WR_point;
extern volatile uint16_t RX_FIFO_RD_point;
extern volatile uint16_t RX_FIFO_last_received;
extern volatile uint16_t RX_size_remaining;

/* TDMA timing */
extern volatile uint32_t TDMA_slave_last_master_top;
extern volatile int32_t TDMA_table_TA[RADIO_ADDR_TABLE_SIZE];

/* Statistics */
extern volatile uint32_t RSSI_total_stat;
extern volatile uint32_t RSSI_stat_pkt_nb;
extern volatile uint32_t RX_Eth_IPv4_counter;
extern volatile uint8_t connect_rejection_reason;

/* Temperature monitoring */
extern volatile uint8_t G_need_temperature_check;
extern volatile uint8_t G_temperature_SI4463;

/* Configuration parameters */
extern int CONF_signaling_period;           /* Signaling period in seconds */
extern uint32_t CONF_radio_timeout_small;   /* Small timeout for radio in microseconds */

/* Downlink signal quality */
extern volatile uint8_t downlink_RSSI;
extern volatile uint16_t downlink_BER;
extern volatile uint16_t G_downlink_RSSI;
extern volatile uint16_t G_downlink_BER;

/* Radio address table statistics */
extern volatile uint16_t G_radio_addr_table_RSSI[RADIO_ADDR_TABLE_SIZE];
extern volatile uint16_t G_radio_addr_table_BER[RADIO_ADDR_TABLE_SIZE];

/* TIM2 microsecond timer */
extern volatile uint32_t g_microsecond_timer;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Get current microsecond timestamp from TIM2
 * @return Current timestamp in microseconds
 */
uint32_t GetMicrosecondTimer(void);

/**
 * @brief Initialize global variables
 */
void InitializeGlobalVariables(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_H */
