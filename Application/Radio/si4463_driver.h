/**
  ******************************************************************************
  * @file    si4463_driver.h
  * @brief   SI4463 radio transceiver driver header
  * @note    Ported from mbed-based implementation to STM32 HAL
  ******************************************************************************
  * This file is part of NPR70 modem firmware
  * Copyright (c) 2017-2020 Guillaume F. F4HDK
  * Copyright (c) 2025 FreeRTOS port
  * Licensed under GPL v3
  ******************************************************************************
  */

#ifndef SI4463_DRIVER_H
#define SI4463_DRIVER_H

#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* Configuration defines */
#define SI4463_OFFSET_SIZE              90
#define SI4463_CONF_RX_FIFO_THRESHOLD   90
#define SI4463_CONF_TX_FIFO_THRESHOLD   90
#define SI4463_CONF_MAX_FIELD2_SIZE     345

/* SI4463 Commands */
#define SI4463_CMD_NOP                  0x00
#define SI4463_CMD_PART_INFO            0x01
#define SI4463_CMD_FUNC_INFO            0x10
#define SI4463_CMD_SET_PROPERTY         0x11
#define SI4463_CMD_GET_PROPERTY         0x12
#define SI4463_CMD_GPIO_PIN_CFG         0x13
#define SI4463_CMD_GET_ADC_READING      0x14
#define SI4463_CMD_FIFO_INFO            0x15
#define SI4463_CMD_GET_INT_STATUS       0x20
#define SI4463_CMD_REQUEST_DEVICE_STATE 0x33
#define SI4463_CMD_CHANGE_STATE         0x34
#define SI4463_CMD_START_TX             0x31
#define SI4463_CMD_START_RX             0x32
#define SI4463_CMD_READ_CMD_BUFF        0x44
#define SI4463_CMD_FRR_A_READ           0x50
#define SI4463_CMD_FRR_B_READ           0x51
#define SI4463_CMD_FRR_C_READ           0x53
#define SI4463_CMD_FRR_D_READ           0x57
#define SI4463_CMD_WRITE_TX_FIFO        0x66
#define SI4463_CMD_READ_RX_FIFO         0x77

/* SI4463 States */
#define SI4463_STATE_SLEEP              0x01
#define SI4463_STATE_SPI_ACTIVE         0x02
#define SI4463_STATE_READY              0x03
#define SI4463_STATE_READY2             0x04
#define SI4463_STATE_TX_TUNE            0x05
#define SI4463_STATE_RX_TUNE            0x06
#define SI4463_STATE_TX                 0x07
#define SI4463_STATE_RX                 0x08

/* SI4463 RX/TX State tracking */
#define SI4463_RXSTATE_IDLE             0
#define SI4463_RXSTATE_RX               1
#define SI4463_RXSTATE_TX               2
#define SI4463_RXSTATE_PREP_TX          3

/* CTS timeout values */
#define SI4463_CTS_TIMEOUT_SHORT        200
#define SI4463_CTS_TIMEOUT_MEDIUM       800
#define SI4463_CTS_TIMEOUT_LONG         5000
#define SI4463_CTS_TIMEOUT_CONFIG       30000

/* Temperature calibration threshold (±15°C) */
#define SI4463_TEMP_RECAL_THRESHOLD     14

/**
  * @brief  SI4463 context structure
  */
typedef struct {
    SPI_HandleTypeDef *hspi;        /* SPI handle */
    GPIO_TypeDef *cs_port;          /* Chip select port */
    uint16_t cs_pin;                /* Chip select pin */
    GPIO_TypeDef *sdn_port;         /* Shutdown port */
    uint16_t sdn_pin;               /* Shutdown pin */
    GPIO_TypeDef *int_port;         /* Interrupt port */
    uint16_t int_pin;               /* Interrupt pin */
    GPIO_TypeDef *rx_led_port;      /* RX LED port (optional) */
    uint16_t rx_led_pin;            /* RX LED pin (optional) */
    SemaphoreHandle_t spi_mutex;    /* SPI bus mutex */
    uint8_t rx_tx_state;            /* Current RX/TX state */
    int16_t last_temperature;       /* Last measured temperature */
} SI4463_Context_t;

/* Function prototypes - Low level */
HAL_StatusTypeDef SI4463_Init(SI4463_Context_t *ctx);
HAL_StatusTypeDef SI4463_Shutdown(SI4463_Context_t *ctx, uint8_t enable);
HAL_StatusTypeDef SI4463_SendCommand(SI4463_Context_t *ctx, const uint8_t *cmd, uint8_t len);
HAL_StatusTypeDef SI4463_ReadCTS(SI4463_Context_t *ctx, uint8_t *data, uint8_t len, uint32_t timeout);
HAL_StatusTypeDef SI4463_GetPartInfo(SI4463_Context_t *ctx, uint8_t *info);
HAL_StatusTypeDef SI4463_GetFuncInfo(SI4463_Context_t *ctx, uint8_t *info);

/* Function prototypes - Configuration */
HAL_StatusTypeDef SI4463_ConfigureFromArray(SI4463_Context_t *ctx, const uint8_t *config_data);
HAL_StatusTypeDef SI4463_SetPower(SI4463_Context_t *ctx, uint8_t power_level);
HAL_StatusTypeDef SI4463_SetPreambleLength(SI4463_Context_t *ctx, uint8_t length);
HAL_StatusTypeDef SI4463_SetProperty(SI4463_Context_t *ctx, uint8_t group, uint8_t num_props, 
                                      uint8_t start_prop, const uint8_t *data);

/* Function prototypes - State management */
HAL_StatusTypeDef SI4463_ChangeState(SI4463_Context_t *ctx, uint8_t new_state);
uint8_t SI4463_GetState(SI4463_Context_t *ctx);
HAL_StatusTypeDef SI4463_ClearInterrupts(SI4463_Context_t *ctx, uint8_t ph_clear, uint8_t modem_clear);

/* Function prototypes - FIFO operations */
HAL_StatusTypeDef SI4463_GetFifoStatus(SI4463_Context_t *ctx, uint8_t *rx_count, 
                                        uint8_t *tx_count, uint8_t reset);
HAL_StatusTypeDef SI4463_WriteTxFifo(SI4463_Context_t *ctx, const uint8_t *data, uint8_t len);
HAL_StatusTypeDef SI4463_ReadRxFifo(SI4463_Context_t *ctx, uint8_t *data, uint8_t len);

/* Function prototypes - TX/RX control */
HAL_StatusTypeDef SI4463_StartRx(SI4463_Context_t *ctx, uint8_t channel);
HAL_StatusTypeDef SI4463_StartTx(SI4463_Context_t *ctx, uint8_t channel, uint16_t size);
HAL_StatusTypeDef SI4463_PrepareTX(SI4463_Context_t *ctx, uint8_t preamble_length);
HAL_StatusTypeDef SI4463_TxToRxTransition(SI4463_Context_t *ctx);

/* Function prototypes - Fast Response Registers */
HAL_StatusTypeDef SI4463_ReadFRR(SI4463_Context_t *ctx, uint8_t *data);

/* Function prototypes - Monitoring */
int16_t SI4463_ReadTemperature(SI4463_Context_t *ctx);
HAL_StatusTypeDef SI4463_CheckTemperatureCalibration(SI4463_Context_t *ctx, uint8_t *needs_recal);

#endif /* SI4463_DRIVER_H */

/************************ (C) COPYRIGHT NPR-70 FreeRTOS Port *****END OF FILE****/
