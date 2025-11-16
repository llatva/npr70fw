/**
  ******************************************************************************
  * @file    app_common.c
  * @brief   Common application variables and functions
  ******************************************************************************
  */

#include "app_common.h"
#include "stm32l4xx_hal.h"
#include "ext_sram_driver.h"

/* External handles (defined in main.c) */
extern TIM_HandleTypeDef htim2;
extern ExtSRAM_Context_t hsram;

/* Global configuration structures */
LAN_conf_T LAN_conf_applied = {
    .LAN_modem_IP = 0xC0A80A01,      /* 192.168.10.1 */
    .DHCP_range_start = 0xC0A80A10,  /* 192.168.10.16 */
    .DHCP_range_size = 16,
    .LAN_subnet_mask = 0xFFFFFF00,   /* 255.255.255.0 */
    .LAN_def_route = 0,
    .LAN_def_route_activ = 0,
    .LAN_DNS_activ = 0,
    .LAN_DNS_value = 0,
    .DHCP_server_active = 0
};

RadioConfig_t CONF_radio = {
    .modulation = 20,
    .default_state_ON_OFF = 0,
    .state_ON_OFF = 0,
    .master_FDD = 0,
    .long_preamble_duration_for_TA = 1000,
    .addr_table_status = {0},
    .addr_table_IP_begin = {0}
};

/* Radio address table (separate arrays) */
char CONF_radio_my_callsign[16] = "MYCALL";
char CONF_radio_master_callsign[16] = "MASTER";
uint32_t CONF_radio_addr_table_IP_begin[RADIO_ADDR_TABLE_SIZE] = {0};
uint32_t CONF_radio_addr_table_IP_size[RADIO_ADDR_TABLE_SIZE] = {0};
char CONF_radio_addr_table_callsign[RADIO_ADDR_TABLE_SIZE][16] = {{0}};
uint8_t CONF_radio_addr_table_status[RADIO_ADDR_TABLE_SIZE] = {0};
uint32_t CONF_radio_addr_table_date[RADIO_ADDR_TABLE_SIZE] = {0};
uint32_t CONF_radio_IP_start = 0xC0A80A00;  /* 192.168.10.0 */
uint32_t CONF_radio_IP_size = 256;
uint32_t CONF_radio_IP_size_requested = 16;
uint8_t CONF_radio_static_IP_requested = 0;

/* Radio signal quality arrays */
volatile uint16_t radio_addr_table_RSSI[RADIO_ADDR_TABLE_SIZE] = {0};
volatile uint16_t radio_addr_table_BER[RADIO_ADDR_TABLE_SIZE] = {0};

/* Global state variables */
volatile uint8_t is_TDMA_master = 0;
volatile uint8_t is_SRAM_ext = 0;
volatile uint8_t is_telnet_active = 0;
volatile uint8_t my_client_radio_connexion_state = 0;
uint8_t my_radio_client_ID = 0;  /* Default client ID */

/* RX FIFO - Move to SRAM2 to save main SRAM1 space (512 bytes saved) */
uint8_t RX_FIFO_data[RX_FIFO_SIZE] PLACE_IN_SRAM2;
volatile uint16_t RX_FIFO_WR_point = 0;
volatile uint16_t RX_FIFO_RD_point = 0;
volatile uint16_t RX_FIFO_last_received = 0;
volatile uint16_t RX_size_remaining = 0;

/* TDMA timing */
volatile uint32_t TDMA_slave_last_master_top = 0;
volatile int32_t TDMA_table_TA[RADIO_ADDR_TABLE_SIZE] = {0};

/* Statistics */
volatile uint32_t RSSI_total_stat = 0;
volatile uint32_t RSSI_stat_pkt_nb = 0;
volatile uint32_t RX_Eth_IPv4_counter = 0;
volatile uint8_t connect_rejection_reason = 0;

/* Temperature monitoring */
volatile uint8_t G_need_temperature_check = 0;
volatile uint8_t G_temperature_SI4463 = 0;

/* Configuration parameters */
int CONF_signaling_period = 3;              /* Default 3 seconds */
uint32_t CONF_radio_timeout_small = 1000000; /* 1 second in microseconds */
uint8_t CONF_radio_network_ID = 0;          /* Default network ID 0 */
uint16_t CONF_frequency_HD = 17000;         /* Default 437.000 MHz (17MHz offset from 420MHz) */

/* Downlink signal quality */
volatile uint8_t downlink_RSSI = 0;
volatile uint16_t downlink_BER = 0;
volatile uint16_t G_downlink_RSSI = 0;
volatile uint16_t G_downlink_BER = 0;

/* Radio address table statistics */
volatile uint16_t G_radio_addr_table_RSSI[RADIO_ADDR_TABLE_SIZE] = {0};
volatile uint16_t G_radio_addr_table_BER[RADIO_ADDR_TABLE_SIZE] = {0};

/* TIM2 microsecond timer overflow counter */
volatile uint32_t g_microsecond_timer_overflow = 0;

/* Active buffer sizes - always use external SRAM (now mandatory) */
uint16_t RX_FIFO_SIZE_ACTIVE = RX_FIFO_SIZE_EXTERNAL;

/**
 * @brief Get active RX FIFO size (always external SRAM size)
 */
uint16_t GetActiveRxFifoSize(void) {
    /* External SRAM is mandatory, always return external size */
    return RX_FIFO_SIZE_EXTERNAL;
}

/**
 * @brief Get active radio packet data size (always external SRAM size)
 */
uint16_t GetActiveRadioPacketDataSize(void) {
    /* External SRAM is mandatory, always return external size */
    return RADIO_PACKET_DATA_SIZE_EXTERNAL;
}

/**
 * @brief Get active ethernet packet data size (always external SRAM size)
 */
uint16_t GetActiveEthernetPacketDataSize(void) {
    /* External SRAM is mandatory, always return external size */
    return ETHERNET_PACKET_DATA_SIZE_EXTERNAL;
}

/**
 * @brief Get current microsecond timestamp from TIM2
 * @return Current timestamp in microseconds (48-bit resolution)
 */
uint32_t GetMicrosecondTimer(void)
{
    uint32_t timer_value;
    uint32_t overflow_count;
    
    /* Read atomically */
    taskENTER_CRITICAL();
    timer_value = __HAL_TIM_GET_COUNTER(&htim2);
    overflow_count = g_microsecond_timer_overflow;
    taskEXIT_CRITICAL();
    
    /* Combine 16-bit overflow counter and 16-bit timer */
    return (overflow_count << 16) | (timer_value & 0xFFFF);
}

/**
 * @brief Initialize global variables
 */
void InitializeGlobalVariables(void)
{
    int i;
    
    /* Set active FIFO size to external SRAM size (mandatory) */
    RX_FIFO_SIZE_ACTIVE = RX_FIFO_SIZE_EXTERNAL;
    
    /* Reset FIFO pointers */
    RX_FIFO_WR_point = 0;
    RX_FIFO_RD_point = 0;
    RX_FIFO_last_received = 0;
    RX_size_remaining = 0;
    
    /* Clear external SRAM RX FIFO area */
    /* Note: This runs before scheduler, so ExtSRAM may not be init'd yet.
     * The actual clearing should happen after SRAM init in main.c */
    if (is_SRAM_ext) {
        /* Clear external SRAM RX FIFO area - use external size */
        uint8_t zero_buf[256] = {0};
        for (uint32_t addr = 0; addr < RX_FIFO_SIZE_EXTERNAL; addr += 256) {
            ExtSRAM_Write(&hsram, zero_buf, SRAM_RX_FIFO_BASE_ADDR + addr, 256);
        }
    }
    /* Note: RX_FIFO_data in internal RAM is not used when external SRAM is present */
    
    /* Reset radio address table */
    for (i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
        CONF_radio.addr_table_status[i] = 0;
        CONF_radio.addr_table_IP_begin[i] = 0;
        TDMA_table_TA[i] = 0;
    }
    
    /* Reset statistics */
    RSSI_total_stat = 0;
    RSSI_stat_pkt_nb = 0;
    RX_Eth_IPv4_counter = 0;
    
    /* Reset state variables */
    is_TDMA_master = 0;
    is_telnet_active = 0;
    my_client_radio_connexion_state = 0;
    G_need_temperature_check = 0;
    TDMA_slave_last_master_top = 0;
}

/**
 * @brief Write data to RX FIFO (always uses external SRAM)
 */
void RX_FIFO_Write(uint16_t offset, const uint8_t *data, uint16_t length) {
    /* Always write to external SRAM (mandatory) */
    ExtSRAM_Write(&hsram, data, SRAM_RX_FIFO_BASE_ADDR + offset, length);
}

/**
 * @brief Read data from RX FIFO (always uses external SRAM)
 */
void RX_FIFO_Read(uint16_t offset, uint8_t *data, uint16_t length) {
    /* Always read from external SRAM (mandatory) */
    ExtSRAM_Read(&hsram, data, SRAM_RX_FIFO_BASE_ADDR + offset, length);
}

/**
 * @brief Write single byte to RX FIFO (always uses external SRAM)
 */
void RX_FIFO_WriteByte(uint16_t offset, uint8_t byte) {
    /* Always write to external SRAM (mandatory) */
    ExtSRAM_Write(&hsram, &byte, SRAM_RX_FIFO_BASE_ADDR + offset, 1);
}

/**
 * @brief Read single byte from RX FIFO (always uses external SRAM)
 */
uint8_t RX_FIFO_ReadByte(uint16_t offset) {
    uint8_t byte;
    /* Always read from external SRAM (mandatory) */
    ExtSRAM_Read(&hsram, &byte, SRAM_RX_FIFO_BASE_ADDR + offset, 1);
    return byte;
}

/************************ (C) COPYRIGHT NPR-70 FreeRTOS Port *****END OF FILE****/
