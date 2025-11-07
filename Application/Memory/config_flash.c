/*
  ******************************************************************************
  * @file    config_flash.c
  * @brief   Configuration storage in flash memory implementation
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "config_flash.h"
#include "FreeRTOS.h"
#include "task.h"

/* Private variables ---------------------------------------------------------*/
/* No static buffer - read directly from flash to save RAM */

/* Private function prototypes -----------------------------------------------*/
static void Config_LoadDefaults(void);
static void Config_ApplyToGlobals(const FlashConfig_t *config);
static void Config_CopyFromGlobals(FlashConfig_t *config);
static HAL_StatusTypeDef Config_Flash_ErasePage(uint32_t page);
static HAL_StatusTypeDef Config_Flash_WritePage(uint32_t addr, const FlashConfig_t *config);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief Load factory default configuration
 */
static void Config_LoadDefaults(void)
{
    /* LAN defaults */
    LAN_conf_applied.LAN_modem_IP = 0xC0A80A01;        /* 192.168.10.1 */
    LAN_conf_applied.DHCP_range_start = 0xC0A80A10;    /* 192.168.10.16 */
    LAN_conf_applied.DHCP_range_size = 16;
    LAN_conf_applied.LAN_subnet_mask = 0xFFFFFF00;     /* 255.255.255.0 */
    LAN_conf_applied.LAN_def_route = 0;
    LAN_conf_applied.LAN_def_route_activ = 0;
    LAN_conf_applied.LAN_DNS_activ = 0;
    LAN_conf_applied.LAN_DNS_value = 0;
    LAN_conf_applied.DHCP_server_active = 0;
    
    /* Radio defaults */
    CONF_radio.modulation = 20;
    CONF_radio.default_state_ON_OFF = 0;
    CONF_radio.state_ON_OFF = 0;
    CONF_radio.master_FDD = 0;
    CONF_radio.long_preamble_duration_for_TA = 1000;
    
    /* Callsigns */
    strncpy(CONF_radio_my_callsign, "MYCALL", 15);
    CONF_radio_my_callsign[15] = '\0';
    strncpy(CONF_radio_master_callsign, "MASTER", 15);
    CONF_radio_master_callsign[15] = '\0';
    
    /* IP configuration */
    CONF_radio_IP_start = 0xC0A80A00;      /* 192.168.10.0 */
    CONF_radio_IP_size = 256;
    CONF_radio_IP_size_requested = 16;
    CONF_radio_static_IP_requested = 0;
    
    /* Signaling */
    CONF_signaling_period = 3;             /* 3 seconds */
    CONF_radio_timeout_small = 1000000;    /* 1 second */
    CONF_radio_network_ID = 0;
    CONF_frequency_HD = 17000;             /* 437.000 MHz */
}

/**
 * @brief Apply configuration to global variables
 */
static void Config_ApplyToGlobals(const FlashConfig_t *config)
{
    /* LAN configuration */
    LAN_conf_applied.LAN_modem_IP = config->LAN_modem_IP;
    LAN_conf_applied.DHCP_range_start = config->DHCP_range_start;
    LAN_conf_applied.DHCP_range_size = config->DHCP_range_size;
    LAN_conf_applied.LAN_subnet_mask = config->LAN_subnet_mask;
    LAN_conf_applied.LAN_def_route = config->LAN_def_route;
    LAN_conf_applied.LAN_def_route_activ = config->LAN_def_route_activ;
    LAN_conf_applied.LAN_DNS_activ = config->LAN_DNS_activ;
    LAN_conf_applied.LAN_DNS_value = config->LAN_DNS_value;
    LAN_conf_applied.DHCP_server_active = config->DHCP_server_active;
    
    /* Radio configuration */
    CONF_radio.modulation = config->radio_modulation;
    CONF_radio.default_state_ON_OFF = config->radio_default_state_ON_OFF;
    CONF_radio.state_ON_OFF = config->radio_default_state_ON_OFF;  /* Start in default state */
    CONF_radio.master_FDD = config->radio_master_FDD;
    CONF_radio.long_preamble_duration_for_TA = config->radio_long_preamble_duration_for_TA;
    
    /* Callsigns */
    strncpy(CONF_radio_my_callsign, config->radio_my_callsign, 15);
    CONF_radio_my_callsign[15] = '\0';
    strncpy(CONF_radio_master_callsign, config->radio_master_callsign, 15);
    CONF_radio_master_callsign[15] = '\0';
    
    /* Radio address table */
    for (int i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
        CONF_radio_addr_table_IP_begin[i] = config->radio_addr_table_IP_begin[i];
        CONF_radio_addr_table_IP_size[i] = config->radio_addr_table_IP_size[i];
        strncpy(CONF_radio_addr_table_callsign[i], config->radio_addr_table_callsign[i], 15);
        CONF_radio_addr_table_callsign[i][15] = '\0';
    }
    
    /* IP configuration */
    CONF_radio_IP_start = config->radio_IP_start;
    CONF_radio_IP_size = config->radio_IP_size;
    CONF_radio_IP_size_requested = config->radio_IP_size_requested;
    CONF_radio_static_IP_requested = config->radio_static_IP_requested;
    
    /* Signaling */
    CONF_signaling_period = config->signaling_period;
    CONF_radio_timeout_small = config->radio_timeout_small;
    CONF_radio_network_ID = config->radio_network_ID;
    CONF_frequency_HD = config->frequency_HD;
}

/**
 * @brief Copy global variables to configuration structure
 */
static void Config_CopyFromGlobals(FlashConfig_t *config)
{
    /* Header */
    config->magic = CONFIG_MAGIC_NUMBER;
    config->version = CONFIG_VERSION;
    
    /* LAN configuration */
    config->LAN_modem_IP = LAN_conf_applied.LAN_modem_IP;
    config->DHCP_range_start = LAN_conf_applied.DHCP_range_start;
    config->DHCP_range_size = LAN_conf_applied.DHCP_range_size;
    config->LAN_subnet_mask = LAN_conf_applied.LAN_subnet_mask;
    config->LAN_def_route = LAN_conf_applied.LAN_def_route;
    config->LAN_def_route_activ = LAN_conf_applied.LAN_def_route_activ;
    config->LAN_DNS_activ = LAN_conf_applied.LAN_DNS_activ;
    config->LAN_DNS_value = LAN_conf_applied.LAN_DNS_value;
    config->DHCP_server_active = LAN_conf_applied.DHCP_server_active;
    
    /* Radio configuration */
    config->radio_modulation = CONF_radio.modulation;
    config->radio_default_state_ON_OFF = CONF_radio.default_state_ON_OFF;
    config->radio_master_FDD = CONF_radio.master_FDD;
    config->radio_long_preamble_duration_for_TA = CONF_radio.long_preamble_duration_for_TA;
    
    /* Callsigns */
    strncpy(config->radio_my_callsign, CONF_radio_my_callsign, 15);
    config->radio_my_callsign[15] = '\0';
    strncpy(config->radio_master_callsign, CONF_radio_master_callsign, 15);
    config->radio_master_callsign[15] = '\0';
    
    /* Radio address table */
    for (int i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
        config->radio_addr_table_IP_begin[i] = CONF_radio_addr_table_IP_begin[i];
        config->radio_addr_table_IP_size[i] = CONF_radio_addr_table_IP_size[i];
        strncpy(config->radio_addr_table_callsign[i], CONF_radio_addr_table_callsign[i], 15);
        config->radio_addr_table_callsign[i][15] = '\0';
    }
    
    /* IP configuration */
    config->radio_IP_start = CONF_radio_IP_start;
    config->radio_IP_size = CONF_radio_IP_size;
    config->radio_IP_size_requested = CONF_radio_IP_size_requested;
    config->radio_static_IP_requested = CONF_radio_static_IP_requested;
    
    /* Signaling */
    config->signaling_period = CONF_signaling_period;
    config->radio_timeout_small = CONF_radio_timeout_small;
    config->radio_network_ID = CONF_radio_network_ID;
    config->frequency_HD = CONF_frequency_HD;
    
    /* Calculate CRC */
    config->crc32 = Config_Calculate_CRC32(config);
}

/**
 * @brief Erase a flash page
 */
static HAL_StatusTypeDef Config_Flash_ErasePage(uint32_t page)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error = 0;
    
    /* Unlock flash */
    HAL_FLASH_Unlock();
    
    /* Configure erase */
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Page = page;
    erase_init.NbPages = 1;
    
    /* Erase page */
    status = HAL_FLASHEx_Erase(&erase_init, &page_error);
    
    /* Lock flash */
    HAL_FLASH_Lock();
    
    return status;
}

/**
 * @brief Write configuration to flash page
 */
static HAL_StatusTypeDef Config_Flash_WritePage(uint32_t addr, const FlashConfig_t *config)
{
    HAL_StatusTypeDef status = HAL_OK;
    const uint64_t *data = (const uint64_t *)config;
    uint32_t num_words = (sizeof(FlashConfig_t) + 7) / 8;  /* Round up to 64-bit words */
    
    /* Unlock flash */
    HAL_FLASH_Unlock();
    
    /* Write data as 64-bit words */
    for (uint32_t i = 0; i < num_words && status == HAL_OK; i++) {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, 
                                   addr + (i * 8), 
                                   data[i]);
    }
    
    /* Lock flash */
    HAL_FLASH_Lock();
    
    return status;
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief Initialize flash configuration module
 */
HAL_StatusTypeDef Config_Flash_Init(void)
{
    /* Just verify flash is accessible */
    return HAL_OK;
}

/**
 * @brief Calculate CRC32 of configuration data
 */
uint32_t Config_Calculate_CRC32(const FlashConfig_t *config)
{
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *data = (const uint8_t *)config;
    
    /* Calculate CRC over entire structure except the CRC field itself */
    /* Skip first 12 bytes (magic, version, crc32) */
    for (size_t i = 12; i < sizeof(FlashConfig_t); i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return ~crc;
}

/**
 * @brief Verify flash configuration integrity
 */
HAL_StatusTypeDef Config_Flash_Verify(uint32_t addr)
{
    const FlashConfig_t *config = (const FlashConfig_t *)addr;
    uint32_t calculated_crc;
    
    /* Check magic number */
    if (config->magic != CONFIG_MAGIC_NUMBER) {
        return HAL_ERROR;
    }
    
    /* Check version */
    if (config->version != CONFIG_VERSION) {
        return HAL_ERROR;
    }
    
    /* Verify CRC */
    calculated_crc = Config_Calculate_CRC32(config);
    if (calculated_crc != config->crc32) {
        return HAL_ERROR;
    }
    
    return HAL_OK;
}

/**
 * @brief Load configuration from flash
 */
HAL_StatusTypeDef Config_Flash_Load(void)
{
    HAL_StatusTypeDef status;
    const FlashConfig_t *flash_config;
    
    /* Try to load from main config page */
    status = Config_Flash_Verify(FLASH_CONFIG_BASE_ADDR);
    
    if (status == HAL_OK) {
        /* Valid config found in main page - apply directly from flash */
        flash_config = (const FlashConfig_t *)FLASH_CONFIG_BASE_ADDR;
        Config_ApplyToGlobals(flash_config);
        return HAL_OK;
    }
    
    /* Try backup page */
    status = Config_Flash_Verify(FLASH_CONFIG_BACKUP_ADDR);
    
    if (status == HAL_OK) {
        /* Valid config found in backup page */
        flash_config = (const FlashConfig_t *)FLASH_CONFIG_BACKUP_ADDR;
        Config_ApplyToGlobals(flash_config);
        
        /* Try to restore main page from backup (don't fail if it doesn't work) */
        FlashConfig_t temp_config;
        memcpy(&temp_config, flash_config, sizeof(FlashConfig_t));
        Config_Flash_ErasePage(FLASH_CONFIG_PAGE);
        Config_Flash_WritePage(FLASH_CONFIG_BASE_ADDR, &temp_config);
        
        return HAL_OK;
    }
    
    /* No valid config found, use defaults */
    Config_LoadDefaults();
    
    /* Save defaults to flash */
    Config_Flash_Save();
    
    return HAL_ERROR;  /* Indicate defaults were loaded */
}

/**
 * @brief Save current configuration to flash
 */
HAL_StatusTypeDef Config_Flash_Save(void)
{
    HAL_StatusTypeDef status;
    FlashConfig_t temp_config;  /* Temporary buffer on stack */
    
    /* Disable interrupts during flash write (use HAL, not FreeRTOS) */
    __disable_irq();
    
    /* Copy current globals to config structure */
    Config_CopyFromGlobals(&temp_config);
    
    /* Erase and write main page */
    status = Config_Flash_ErasePage(FLASH_CONFIG_PAGE);
    if (status == HAL_OK) {
        status = Config_Flash_WritePage(FLASH_CONFIG_BASE_ADDR, &temp_config);
    }
    
    /* If main page write successful, also update backup */
    if (status == HAL_OK) {
        Config_Flash_ErasePage(FLASH_CONFIG_BACKUP_PAGE);
        Config_Flash_WritePage(FLASH_CONFIG_BACKUP_ADDR, &temp_config);
    }
    
    /* Re-enable interrupts */
    __enable_irq();
    
    return status;
}

/**
 * @brief Restore factory default configuration
 */
HAL_StatusTypeDef Config_Flash_FactoryReset(void)
{
    /* Load defaults into global variables */
    Config_LoadDefaults();
    
    /* Save to flash */
    return Config_Flash_Save();
}
