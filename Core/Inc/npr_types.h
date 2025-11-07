/**
 ******************************************************************************
 * @file           : npr_types.h
 * @brief          : Common types and structures for NPR-70 FreeRTOS port
 ******************************************************************************
 */

#ifndef NPR_TYPES_H
#define NPR_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

/* ============================================================================
 * Radio Packet Structures
 * ============================================================================ */

/**
 * @brief Radio packet descriptor - used in queues between radio and ethernet
 */
typedef struct {
    uint32_t timestamp_us;      /* Microsecond timestamp from TDMA timer */
    uint16_t size;              /* Packet size */
    uint8_t  rssi;              /* Received signal strength (RX only) */
    uint8_t  client_id;         /* TDMA client ID */
    uint8_t  protocol;          /* Protocol type */
    uint8_t  tdma_byte;         /* TDMA control byte */
    uint16_t micro_ber;         /* Bit error rate (RX only) */
    bool     is_downlink;       /* Master downlink flag */
    uint8_t  data[MAX_RADIO_PACKET_SIZE];  /* Packet payload */
} RadioPacket_t;

/**
 * @brief Ethernet packet descriptor
 */
typedef struct {
    uint16_t size;              /* Packet size */
    uint8_t  socket_id;         /* W5500 socket number */
    uint32_t dest_ip;           /* Destination IP (for TX) */
    uint16_t dest_port;         /* Destination port (for TX) */
    uint8_t  data[MAX_ETH_PACKET_SIZE];  /* Packet payload */
} EthPacket_t;

/**
 * @brief Radio ISR event - minimal data passed from ISR to task
 */
typedef struct {
    uint8_t  event_type;        /* RX_SYNC, RX_FIFO_FULL, RX_COMPLETE, TX_FIFO_EMPTY, TX_COMPLETE */
    uint32_t timestamp_us;      /* Event timestamp */
    uint8_t  frr[4];            /* Fast Response Registers from SI4463 */
} RadioISREvent_t;

/* Radio ISR event types */
#define RADIO_ISR_EVENT_RX_SYNC         0x01
#define RADIO_ISR_EVENT_RX_FIFO_FULL    0x02
#define RADIO_ISR_EVENT_RX_COMPLETE     0x04
#define RADIO_ISR_EVENT_TX_FIFO_EMPTY   0x08
#define RADIO_ISR_EVENT_TX_COMPLETE     0x10

/* ============================================================================
 * Hardware Driver Structures
 * ============================================================================ */

/**
 * @brief SI4463 radio chip context
 */
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
    GPIO_TypeDef      *sdn_port;
    uint16_t           sdn_pin;
    GPIO_TypeDef      *int_port;
    uint16_t           int_pin;
    GPIO_TypeDef      *led_rx_port;
    uint16_t           led_rx_pin;
    
    uint8_t            rx_tx_state;     /* 0=idle, 1=RX, 2=TX */
    SemaphoreHandle_t  mutex;           /* SPI access mutex */
    QueueHandle_t      isr_queue;       /* ISR event queue */
} SI4463_Context_t;

/**
 * @brief W5500 Ethernet chip context
 */
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
    GPIO_TypeDef      *int_port;
    uint16_t           int_pin;
    
    uint8_t            sock_interrupt;  /* Socket interrupt status */
    SemaphoreHandle_t  mutex;           /* SPI access mutex */
    QueueHandle_t      isr_queue;       /* W5500 interrupt events */
} W5500_Context_t;

/**
 * @brief External SRAM chip context
 */
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
    
    bool               is_detected;     /* SRAM presence flag */
    SemaphoreHandle_t  mutex;           /* SPI access mutex (shared with W5500) */
} ExtSRAM_Context_t;

/* ============================================================================
 * TDMA and Radio Configuration
 * ============================================================================ */

/**
 * @brief TDMA timing configuration
 */
typedef struct {
    uint32_t frame_duration;            /* Frame duration in microseconds */
    uint32_t slot_duration;             /* Slot duration in microseconds */
    uint32_t reduced_slot_duration;     /* Reduced slot duration */
    uint32_t slot_margin;               /* Timing margin */
    uint32_t tr_margin;                 /* TX/RX transition margin */
    uint32_t ta_margin;                 /* Timing advance margin */
    uint32_t preamble_duration_decide;  /* Preamble duration for decisions */
    uint32_t long_preamble_duration_ta; /* Long preamble for TA measurement */
    uint32_t byte_duration;             /* Single byte transmission time */
    uint32_t additional_preamble;       /* Additional preamble bytes */
    uint32_t radio_timeout;             /* Radio timeout in microseconds */
    uint32_t radio_timeout_small;       /* Small timeout */
    uint8_t  preamble_tx_long;          /* Long preamble length */
    uint8_t  preamble_tx_short;         /* Short preamble length */
} TDMA_Config_t;

/**
 * @brief Radio client table entry
 */
typedef struct {
    bool     active;                    /* Entry is active */
    uint8_t  client_id;                 /* Client ID (0-15) */
    char     callsign[16];              /* Amateur radio callsign */
    uint32_t ip_start;                  /* IP address range start */
    uint32_t ip_size;                   /* IP address range size */
    uint8_t  mac_addr[6];               /* MAC address */
    
    /* TDMA state */
    uint8_t  allocated_slots;           /* Number of allocated slots */
    uint32_t slot_offset;               /* Timing offset (microseconds) */
    int32_t  timing_advance;            /* TA value (filtered) */
    uint32_t last_rx_time;              /* Last reception timestamp */
    uint8_t  uplink_buffer_status;      /* Uplink buffer status from client */
    bool     is_fast_mode;              /* Fast slot allocation */
    
    /* Statistics */
    uint8_t  rssi_avg;                  /* Average RSSI */
    uint16_t micro_ber_avg;             /* Average BER */
    uint32_t packets_rx;                /* Packets received */
    uint32_t packets_tx;                /* Packets transmitted */
} RadioClient_t;

/**
 * @brief LAN/Network configuration
 */
typedef struct {
    uint32_t modem_ip;                  /* Modem IP address */
    uint32_t netmask;                   /* Network mask */
    uint32_t gateway;                   /* Gateway */
    uint8_t  modem_mac[6];              /* Modem MAC address */
    
    bool     dhcp_server_active;        /* DHCP server enabled */
    uint32_t dhcp_pool_start;           /* DHCP pool start IP */
    uint32_t dhcp_pool_size;            /* DHCP pool size */
    
    bool     telnet_enabled;            /* Telnet server enabled */
    uint16_t telnet_port;               /* Telnet port (default 23) */
    
    bool     snmp_enabled;              /* SNMP agent enabled */
    uint16_t snmp_port;                 /* SNMP port (default 161) */
} LAN_Config_t;

/**
 * @brief System configuration (stored in flash)
 */
typedef struct {
    /* Radio settings */
    uint8_t  radio_modulation;          /* Modulation scheme (12,13,14,22,23,24) */
    uint16_t radio_frequency;           /* Frequency in channel units */
    uint8_t  radio_network_id;          /* Network ID (0-15) */
    uint8_t  radio_pa_power;            /* PA power (0-127) */
    uint8_t  channel_tx;                /* TX channel */
    uint8_t  channel_rx;                /* RX channel */
    int16_t  freq_shift;                /* Frequency shift */
    char     radio_my_callsign[16];     /* This modem's callsign */
    
    /* TDMA settings */
    bool     is_tdma_master;            /* Master/slave mode */
    uint8_t  master_fdd;                /* FDD mode (0=TDD, 1=FDD_down, 2=FDD_up) */
    uint32_t master_down_ip;            /* Master downlink IP (for FDD) */
    
    /* Network settings */
    LAN_Config_t lan_config;
    
    /* Misc */
    uint8_t  signaling_period;          /* Signaling period (seconds) */
    bool     radio_state_on_off;        /* Radio enabled */
    bool     radio_default_state;       /* Default radio state on boot */
    
    /* Configuration validity marker */
    uint32_t config_magic;              /* Magic number for valid config */
    uint32_t config_crc;                /* CRC32 of configuration */
} SystemConfig_t;

#define CONFIG_MAGIC_NUMBER 0x4E505237  /* 'NPR7' */

/* ============================================================================
 * Queue Handles (declared in main.c, extern here)
 * ============================================================================ */

extern QueueHandle_t xQueueRadioToEth;      /* Radio RX -> Ethernet TX */
extern QueueHandle_t xQueueEthToRadio;      /* Ethernet RX -> Radio TX */
extern QueueHandle_t xQueueRadioISR;        /* SI4463 ISR -> Radio task */
extern QueueHandle_t xQueueW5500ISR;        /* W5500 ISR -> Ethernet task */

/* ============================================================================
 * Global Context Instances (declared in main.c, extern here)
 * ============================================================================ */

extern SI4463_Context_t g_si4463;
extern W5500_Context_t  g_w5500;
extern ExtSRAM_Context_t g_ext_sram;
extern SystemConfig_t   g_config;
extern TDMA_Config_t    g_tdma_config;
extern RadioClient_t    g_radio_clients[RADIO_ADDR_TABLE_SIZE];

#ifdef __cplusplus
}
#endif

#endif /* NPR_TYPES_H */
