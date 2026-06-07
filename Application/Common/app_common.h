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

#define PLACE_IN_SRAM2 __attribute__((section(".sram2")))

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
#include "ext_sram_driver.h"  /* For ExtSRAM_Context_t */

/* Radio configuration constants - from si4463_driver.h */
#define RADIO_ADDR_TABLE_SIZE 4  /* Reduced from 16 to save heap (4×1600 = 6.4KB vs 25.6KB) */

/* RX FIFO configuration - EXTERNAL SRAM IS MANDATORY */
/* Always use larger buffers since external SRAM is required */
#define RX_FIFO_SIZE_INTERNAL  0x200   /* 512B (not used, kept for compatibility) */
#define RX_FIFO_SIZE_EXTERNAL  0x800   /* 2KB - ALWAYS USED (external SRAM mandatory) */

/* Active RX FIFO size - always external since SRAM is mandatory */
extern uint16_t RX_FIFO_SIZE_ACTIVE;
#define RX_FIFO_SIZE RX_FIFO_SIZE_INTERNAL  /* Compile-time size for static array (legacy, not used) */
#define RX_FIFO_MASK (RX_FIFO_SIZE - 1)

/* Queue sizes - always use external sizes (SRAM mandatory) */
#define RADIO_ISR_QUEUE_SIZE 8
#define RADIO_TX_QUEUE_SIZE_INTERNAL     2   /* Not used (kept for compatibility) */
#define RADIO_TX_QUEUE_SIZE_EXTERNAL     4   /* Always used */
#define ETHERNET_RX_QUEUE_SIZE_INTERNAL  1   /* Not used (kept for compatibility) */
#define ETHERNET_RX_QUEUE_SIZE_EXTERNAL  2   /* Always used */
#define ETHERNET_TX_QUEUE_SIZE_INTERNAL  1   /* Not used (kept for compatibility) */
#define ETHERNET_TX_QUEUE_SIZE_EXTERNAL  2   /* Always used */

/* Packet buffer sizes - always use external sizes (SRAM mandatory) */
#define RADIO_PACKET_DATA_SIZE_INTERNAL   256  /* Not used (kept for compatibility) */
#define RADIO_PACKET_DATA_SIZE_EXTERNAL   384  /* Always used */
#define ETHERNET_PACKET_DATA_SIZE_INTERNAL 512 /* Not used (kept for compatibility) */
#define ETHERNET_PACKET_DATA_SIZE_EXTERNAL 1600 /* Always used - full MTU */

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
 * Note: Uses INTERNAL size for static allocation. Runtime checks limit actual usage.
 */
typedef struct {
    uint32_t timestamp;     /* Reception timestamp */
    uint8_t rssi;           /* RSSI value */
    uint16_t length;        /* Packet length */
    uint8_t data[RADIO_PACKET_DATA_SIZE_INTERNAL];      /* Packet data - sized for internal RAM */
} RadioRxPacket_t;

/**
 * @brief Ethernet packet structure  
 * Note: Uses INTERNAL size for static allocation. Runtime checks limit actual usage.
 */
typedef struct {
    uint16_t socket;        /* Socket number */
    uint16_t length;        /* Packet length */
    uint8_t data[ETHERNET_PACKET_DATA_SIZE_INTERNAL];     /* Packet data - sized for internal RAM */
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

/* Driver handles - defined in main.c */
extern ExtSRAM_Context_t hsram;

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

/* External SRAM configuration */
#define SRAM_RX_FIFO_BASE_ADDR  0x00000000  /* RX FIFO starts at address 0 in SRAM */

/* TDMA timing */
extern volatile uint32_t TDMA_slave_last_master_top;
extern volatile int32_t TDMA_table_TA[RADIO_ADDR_TABLE_SIZE];

/* TDMA allocation and timing (master mode) */
extern volatile uint32_t time_max_TX_burst;         /* Maximum TX burst time in microseconds */
extern volatile uint32_t offset_time_TX_slave;      /* Client TX timing offset */
extern volatile uint32_t TDMA_offset_multi_frame;   /* Multi-frame timing offset */
extern volatile uint8_t master_allocated_slots;     /* Number of slots allocated to master */

/* TDMA configuration parameters (timing in microseconds) */
extern uint32_t CONF_TDMA_frame_duration;
extern uint32_t CONF_TDMA_slot_duration;
extern uint32_t CONF_reduced_TDMA_slot_duration;
extern uint32_t CONF_TDMA_slot_margin;
extern uint32_t CONF_TR_margain;
extern uint32_t CONF_TA_margain;
extern uint32_t CONF_delay_prepTX1_2_TX;

/* TDMA TX buffers */
extern uint8_t TX_TDMA_intern_data[400];             /* TDMA allocation frame buffer */

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
extern uint8_t CONF_radio_network_ID;       /* Radio network ID (0-255) */
extern uint16_t CONF_frequency_HD;          /* Radio frequency in kHz offset from band start */
extern uint8_t CONF_modem_MAC[6];           /* Modem MAC address (default NFPR:xx:xx) */

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
 * @brief Get active RX FIFO size based on SRAM configuration
 * @return Current active RX FIFO size (512B internal or 2KB external)
 */
uint16_t GetActiveRxFifoSize(void);

/**
 * @brief Get active radio packet data size based on SRAM configuration
 * @return Current active packet data size (256B internal or 384B external)
 */
uint16_t GetActiveRadioPacketDataSize(void);

/**
 * @brief Get active ethernet packet data size based on SRAM configuration
 * @return Current active packet data size (512B internal or 1600B external)
 */
uint16_t GetActiveEthernetPacketDataSize(void);

/**
 * @brief Get current microsecond timestamp from TIM2
 * @return Current timestamp in microseconds
 */
uint32_t GetMicrosecondTimer(void);

/**
 * @brief Initialize global variables
 */
void InitializeGlobalVariables(void);

/**
 * @brief Write data to RX FIFO (handles internal RAM or external SRAM)
 * @param offset Offset in RX FIFO buffer
 * @param data Pointer to data to write
 * @param length Length of data to write
 */
void RX_FIFO_Write(uint16_t offset, const uint8_t *data, uint16_t length);

/**
 * @brief Read data from RX FIFO (handles internal RAM or external SRAM)
 * @param offset Offset in RX FIFO buffer
 * @param data Pointer to buffer to read into
 * @param length Length of data to read
 */
void RX_FIFO_Read(uint16_t offset, uint8_t *data, uint16_t length);

/**
 * @brief Write single byte to RX FIFO
 * @param offset Offset in RX FIFO buffer
 * @param byte Byte to write
 */
void RX_FIFO_WriteByte(uint16_t offset, uint8_t byte);

/**
 * @brief Read single byte from RX FIFO
 * @param offset Offset in RX FIFO buffer
 * @return Byte read from FIFO
 */
uint8_t RX_FIFO_ReadByte(uint16_t offset);

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_H */
