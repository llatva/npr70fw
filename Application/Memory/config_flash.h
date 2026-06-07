/*
  ******************************************************************************
  * @file    config_flash.h
  * @brief   Configuration storage in flash memory
  ******************************************************************************
  * @attention
  *
  * Stores NPR-70 configuration in the last pages of flash memory
  * STM32L432KC has 256KB flash (128 pages of 2KB each)
  * We use the last 2 pages (254-255) for configuration storage
  *
  ******************************************************************************
  */

#ifndef CONFIG_FLASH_H
#define CONFIG_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"
#include "app_common.h"
#include <stdint.h>
#include <string.h>

/* Exported defines ----------------------------------------------------------*/
/* STM32L432KC Flash organization:
 * - Total: 256KB (0x08000000 - 0x0803FFFF)
 * - Page size: 2KB (2048 bytes)
 * - Total pages: 128 (0-127)
 * - We use last 2 pages for config (pages 126-127)
 */
#ifndef FLASH_PAGE_SIZE
 #define FLASH_PAGE_SIZE         2048
#endif
#define FLASH_CONFIG_PAGE       126     /* Page 126 for main config */
#define FLASH_CONFIG_BACKUP_PAGE 127    /* Page 127 for backup */
#define FLASH_CONFIG_BASE_ADDR  (FLASH_BASE + (FLASH_CONFIG_PAGE * FLASH_PAGE_SIZE))
#define FLASH_CONFIG_BACKUP_ADDR (FLASH_BASE + (FLASH_CONFIG_BACKUP_PAGE * FLASH_PAGE_SIZE))

/* Magic number to verify valid configuration */
#define CONFIG_MAGIC_NUMBER     0x4E505237  /* "NPR7" */
#define CONFIG_VERSION          1

/* Exported types ------------------------------------------------------------*/

/**
 * @brief Configuration structure stored in flash
 * 
 * This structure contains all user-configurable parameters that should
 * persist across reboots. Total size should be < 2KB (one flash page)
 */
typedef struct __attribute__((packed)) {
    /* Header */
    uint32_t magic;                     /* Magic number for validation */
    uint32_t version;                   /* Configuration version */
    uint32_t crc32;                     /* CRC32 of config data (excluding header) */
    
    /* LAN Configuration */
    uint32_t LAN_modem_IP;
    uint32_t DHCP_range_start;
    uint32_t DHCP_range_size;
    uint32_t LAN_subnet_mask;
    uint32_t LAN_def_route;
    uint8_t LAN_def_route_activ;
    uint8_t LAN_DNS_activ;
    uint32_t LAN_DNS_value;
    uint8_t DHCP_server_active;
    
    /* Radio Configuration */
    uint8_t radio_modulation;
    uint8_t radio_default_state_ON_OFF;
    uint8_t radio_master_FDD;
    uint16_t radio_long_preamble_duration_for_TA;
    uint8_t radio_network_ID;
    uint16_t frequency_HD;              /* Frequency offset in 100Hz units */
    
    /* Radio Callsigns */
    char radio_my_callsign[16];
    char radio_master_callsign[16];
    
    /* Radio Address Table */
    uint32_t radio_addr_table_IP_begin[RADIO_ADDR_TABLE_SIZE];
    uint32_t radio_addr_table_IP_size[RADIO_ADDR_TABLE_SIZE];
    char radio_addr_table_callsign[RADIO_ADDR_TABLE_SIZE][16];
    
    /* IP Configuration */
    uint32_t radio_IP_start;
    uint32_t radio_IP_size;
    uint32_t radio_IP_size_requested;
    uint8_t radio_static_IP_requested;
    
    /* Signaling */
    int32_t signaling_period;           /* Signaling period in seconds */
    uint32_t radio_timeout_small;       /* Radio timeout in microseconds */
    
    /* Padding to ensure alignment */
    uint8_t reserved[64];               /* Reserved for future use */
    
} FlashConfig_t;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize flash configuration module
 * @return HAL_OK if successful, HAL_ERROR otherwise
 */
HAL_StatusTypeDef Config_Flash_Init(void);

/**
 * @brief Load configuration from flash
 * @return HAL_OK if valid config loaded, HAL_ERROR if using defaults
 */
HAL_StatusTypeDef Config_Flash_Load(void);

/**
 * @brief Save current configuration to flash
 * @return HAL_OK if successful, HAL_ERROR otherwise
 */
HAL_StatusTypeDef Config_Flash_Save(void);

/**
 * @brief Restore factory default configuration
 * @return HAL_OK if successful, HAL_ERROR otherwise
 */
HAL_StatusTypeDef Config_Flash_FactoryReset(void);

/**
 * @brief Verify flash configuration integrity
 * @param addr Flash address to verify
 * @return HAL_OK if valid, HAL_ERROR otherwise
 */
HAL_StatusTypeDef Config_Flash_Verify(uint32_t addr);

/**
 * @brief Calculate CRC32 of configuration data
 * @param config Pointer to configuration structure
 * @return CRC32 value
 */
uint32_t Config_Calculate_CRC32(const FlashConfig_t *config);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_FLASH_H */
