/**
  ******************************************************************************
  * @file    si4463_driver.c
  * @brief   SI4463 radio transceiver driver implementation
  * @note    Ported from mbed-based implementation to STM32 HAL
  ******************************************************************************
  * This file is part of NPR70 modem firmware
  * Copyright (c) 2017-2020 Guillaume F. F4HDK
  * Copyright (c) 2025 FreeRTOS port
  * Licensed under GPL v3
  ******************************************************************************
  */

#include "si4463_driver.h"
#include "FreeRTOS.h"
#include "task.h"

/* Private defines */
#define SI4463_SPI_TIMEOUT      100  /* SPI timeout in ms */
#define SI4463_CTS_READY        0xFF /* Clear To Send ready value */

/* Private function prototypes */
static inline void SI4463_CS_Low(SI4463_Context_t *ctx);
static inline void SI4463_CS_High(SI4463_Context_t *ctx);
static HAL_StatusTypeDef SI4463_SPI_Lock(SI4463_Context_t *ctx);
static void SI4463_SPI_Unlock(SI4463_Context_t *ctx);

/**
  * @brief  Initialize SI4463 device
  */
HAL_StatusTypeDef SI4463_Init(SI4463_Context_t *ctx)
{
    if (ctx == NULL || ctx->hspi == NULL || ctx->spi_mutex == NULL) {
        return HAL_ERROR;
    }

    /* Initialize state */
    ctx->rx_tx_state = SI4463_RXSTATE_IDLE;
    ctx->last_temperature = 300; /* Invalid initial value */
    
    /* Ensure CS is high (inactive) */
    HAL_GPIO_WritePin(ctx->cs_port, ctx->cs_pin, GPIO_PIN_SET);
    
    /* Ensure SDN is low (active, chip enabled) */
    HAL_GPIO_WritePin(ctx->sdn_port, ctx->sdn_pin, GPIO_PIN_RESET);
    
    /* Small delay for radio to stabilize - use busy wait instead of HAL_Delay */
    for (volatile uint32_t i = 0; i < 160000; i++);  /* ~20ms at 80MHz */
    
    return HAL_OK;
}

/**
  * @brief  Control shutdown pin (power down radio)
  */
HAL_StatusTypeDef SI4463_Shutdown(SI4463_Context_t *ctx, uint8_t enable)
{
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    /* SDN pin is active high (high = shutdown) */
    HAL_GPIO_WritePin(ctx->sdn_port, ctx->sdn_pin, 
                      enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
    
    /* Delay for chip to respond (use HAL_Delay if scheduler not started) */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(10));
    } else {
        HAL_Delay(10);
    }
    
    return HAL_OK;
}

/**
  * @brief  Send command to SI4463
  */
HAL_StatusTypeDef SI4463_SendCommand(SI4463_Context_t *ctx, const uint8_t *cmd, uint8_t len)
{
    HAL_StatusTypeDef status;
    uint8_t dummy_rx[32];
    
    if (ctx == NULL || cmd == NULL || len == 0 || len > 32) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus */
    if (SI4463_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Activate CS */
    SI4463_CS_Low(ctx);
    
    /* Send command */
    status = HAL_SPI_TransmitReceive(ctx->hspi, (uint8_t *)cmd, dummy_rx, len, SI4463_SPI_TIMEOUT);
    
    /* Deactivate CS */
    SI4463_CS_High(ctx);
    
    /* Small delay */
    for (volatile int i = 0; i < 10; i++);
    
    /* Unlock SPI */
    SI4463_SPI_Unlock(ctx);
    
    return status;
}

/**
  * @brief  Read CTS (Clear To Send) and optional response data
  * @note   Polls CTS until ready or timeout
  */
HAL_StatusTypeDef SI4463_ReadCTS(SI4463_Context_t *ctx, uint8_t *data, uint8_t len, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint8_t tx_buf[2] = {SI4463_CMD_READ_CMD_BUFF, 0x00};
    uint8_t rx_buf[2];
    uint32_t loops = 0;
    uint32_t max_loops = timeout;
    
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus */
    if (SI4463_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Poll CTS until ready */
    SI4463_CS_Low(ctx);
    status = HAL_SPI_TransmitReceive(ctx->hspi, tx_buf, rx_buf, 2, SI4463_SPI_TIMEOUT);
    
    while (rx_buf[1] != SI4463_CTS_READY && loops < max_loops) {
        SI4463_CS_High(ctx);
        
        /* Small delay between polls */
        for (volatile int i = 0; i < 200; i++);
        
        SI4463_CS_Low(ctx);
        status = HAL_SPI_TransmitReceive(ctx->hspi, tx_buf, rx_buf, 2, SI4463_SPI_TIMEOUT);
        loops++;
    }
    
    /* If data requested, read it */
    if (len > 0 && data != NULL && rx_buf[1] == SI4463_CTS_READY) {
        uint8_t dummy_tx[32] = {0};
        status = HAL_SPI_TransmitReceive(ctx->hspi, dummy_tx, data, len, SI4463_SPI_TIMEOUT);
    }
    
    SI4463_CS_High(ctx);
    
    /* Small delay */
    for (volatile int i = 0; i < 10; i++);
    
    /* Unlock SPI */
    SI4463_SPI_Unlock(ctx);
    
    /* Check if timeout occurred */
    if (loops >= max_loops) {
        return HAL_TIMEOUT;
    }
    
    return status;
}

/**
  * @brief  Get part information
  */
HAL_StatusTypeDef SI4463_GetPartInfo(SI4463_Context_t *ctx, uint8_t *info)
{
    uint8_t cmd = SI4463_CMD_PART_INFO;
    HAL_StatusTypeDef status;
    
    status = SI4463_SendCommand(ctx, &cmd, 1);
    if (status != HAL_OK) {
        return status;
    }
    
    return SI4463_ReadCTS(ctx, info, 8, SI4463_CTS_TIMEOUT_SHORT);
}

/**
  * @brief  Get function information
  */
HAL_StatusTypeDef SI4463_GetFuncInfo(SI4463_Context_t *ctx, uint8_t *info)
{
    uint8_t cmd = SI4463_CMD_FUNC_INFO;
    HAL_StatusTypeDef status;
    
    status = SI4463_SendCommand(ctx, &cmd, 1);
    if (status != HAL_OK) {
        return status;
    }
    
    return SI4463_ReadCTS(ctx, info, 6, SI4463_CTS_TIMEOUT_SHORT);
}

/**
  * @brief  Configure SI4463 from configuration array
  * @note   Configuration data format: [length][cmd][param1][param2]...[length][cmd]...
  *         Terminated by length=0
  */
HAL_StatusTypeDef SI4463_ConfigureFromArray(SI4463_Context_t *ctx, const uint8_t *config_data)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint32_t index = 0;
    uint8_t cmd_len;
    
    if (ctx == NULL || config_data == NULL) {
        return HAL_ERROR;
    }
    
    /* Process configuration commands */
    do {
        cmd_len = config_data[index];
        index++;
        
        if (cmd_len > 0) {
            /* Send command */
            status = SI4463_SendCommand(ctx, &config_data[index], cmd_len);
            if (status != HAL_OK) {
                return status;
            }
            
            /* Wait for CTS */
            status = SI4463_ReadCTS(ctx, NULL, 0, SI4463_CTS_TIMEOUT_CONFIG);
            if (status != HAL_OK) {
                return status;
            }
        }
        
        index += cmd_len;
        
    } while (cmd_len > 0);
    
    vTaskDelay(pdMS_TO_TICKS(5));
    
    /* Configure GLOBAL_CONFIG: SEQUENCER_MODE=GUARANTEED and FIFO_MODE=FIFO_129 */
    uint8_t global_config[] = {0x11, 0x00, 0x01, 0x03, 0x10};
    status = SI4463_SendCommand(ctx, global_config, 5);
    if (status != HAL_OK) {
        return status;
    }
    status = SI4463_ReadCTS(ctx, NULL, 0, SI4463_CTS_TIMEOUT_MEDIUM);
    if (status != HAL_OK) {
        return status;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    
    /* Set Max field 2 size */
    uint8_t field2_config[] = {0x11, 0x12, 0x02, 0x11, 0x00, 0xFF};
    field2_config[4] = (SI4463_CONF_MAX_FIELD2_SIZE & 0x1F00) >> 8;
    field2_config[5] = SI4463_CONF_MAX_FIELD2_SIZE & 0x00FF;
    status = SI4463_SendCommand(ctx, field2_config, 6);
    if (status != HAL_OK) {
        return status;
    }
    status = SI4463_ReadCTS(ctx, NULL, 0, SI4463_CTS_TIMEOUT_MEDIUM);
    if (status != HAL_OK) {
        return status;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    
    /* Set FIFO thresholds */
    uint8_t fifo_config[] = {0x11, 0x12, 0x02, 0x0B, 0x30, 0x64};
    fifo_config[4] = SI4463_CONF_TX_FIFO_THRESHOLD & 0x7F;
    fifo_config[5] = SI4463_CONF_RX_FIFO_THRESHOLD & 0x7F;
    status = SI4463_SendCommand(ctx, fifo_config, 6);
    if (status != HAL_OK) {
        return status;
    }
    status = SI4463_ReadCTS(ctx, NULL, 0, SI4463_CTS_TIMEOUT_MEDIUM);
    if (status != HAL_OK) {
        return status;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    
    return HAL_OK;
}

/**
  * @brief  Set transmit power level
  */
HAL_StatusTypeDef SI4463_SetPower(SI4463_Context_t *ctx, uint8_t power_level)
{
    uint8_t cmd[] = {0x11, 0x22, 0x01, 0x01, 0x00};
    HAL_StatusTypeDef status;
    
    cmd[4] = power_level & 0x7F;
    
    status = SI4463_SendCommand(ctx, cmd, 5);
    if (status != HAL_OK) {
        return status;
    }
    
    status = SI4463_ReadCTS(ctx, NULL, 0, SI4463_CTS_TIMEOUT_MEDIUM);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    return status;
}

/**
  * @brief  Set preamble length
  */
HAL_StatusTypeDef SI4463_SetPreambleLength(SI4463_Context_t *ctx, uint8_t length)
{
    uint8_t cmd[] = {0x11, 0x10, 0x01, 0x00, 0x00};
    HAL_StatusTypeDef status;
    
    cmd[4] = length;
    
    status = SI4463_SendCommand(ctx, cmd, 5);
    if (status != HAL_OK) {
        return status;
    }
    
    /* Small delay before CTS */
    for (volatile int i = 0; i < 200; i++);
    
    return SI4463_ReadCTS(ctx, NULL, 0, SI4463_CTS_TIMEOUT_SHORT);
}

/**
  * @brief  Set radio property
  */
HAL_StatusTypeDef SI4463_SetProperty(SI4463_Context_t *ctx, uint8_t group, uint8_t num_props,
                                      uint8_t start_prop, const uint8_t *data)
{
    uint8_t cmd[20];
    HAL_StatusTypeDef status;
    
    if (num_props > 16 || data == NULL) {
        return HAL_ERROR;
    }
    
    cmd[0] = SI4463_CMD_SET_PROPERTY;
    cmd[1] = group;
    cmd[2] = num_props;
    cmd[3] = start_prop;
    
    for (uint8_t i = 0; i < num_props; i++) {
        cmd[4 + i] = data[i];
    }
    
    status = SI4463_SendCommand(ctx, cmd, 4 + num_props);
    if (status != HAL_OK) {
        return status;
    }
    
    return SI4463_ReadCTS(ctx, NULL, 0, SI4463_CTS_TIMEOUT_MEDIUM);
}

/**
  * @brief  Change radio state
  */
HAL_StatusTypeDef SI4463_ChangeState(SI4463_Context_t *ctx, uint8_t new_state)
{
    uint8_t cmd[] = {SI4463_CMD_CHANGE_STATE, 0x00};
    HAL_StatusTypeDef status;
    
    cmd[1] = new_state;
    
    status = SI4463_SendCommand(ctx, cmd, 2);
    if (status != HAL_OK) {
        return status;
    }
    
    return SI4463_ReadCTS(ctx, NULL, 0, SI4463_CTS_TIMEOUT_SHORT);
}

/**
  * @brief  Get current radio state
  */
uint8_t SI4463_GetState(SI4463_Context_t *ctx)
{
    uint8_t cmd = SI4463_CMD_REQUEST_DEVICE_STATE;
    uint8_t state_data[2];
    
    if (SI4463_SendCommand(ctx, &cmd, 1) != HAL_OK) {
        return 0xFF;
    }
    
    if (SI4463_ReadCTS(ctx, state_data, 2, SI4463_CTS_TIMEOUT_SHORT) != HAL_OK) {
        return 0xFF;
    }
    
    return state_data[0];
}

/**
  * @brief  Clear interrupts
  */
HAL_StatusTypeDef SI4463_ClearInterrupts(SI4463_Context_t *ctx, uint8_t ph_clear, uint8_t modem_clear)
{
    uint8_t cmd[] = {SI4463_CMD_GET_INT_STATUS, 0x00, 0x00, 0x00};
    
    cmd[1] = ph_clear;
    cmd[2] = modem_clear;
    
    return SI4463_SendCommand(ctx, cmd, 4);
}

/**
  * @brief  Get FIFO status
  */
HAL_StatusTypeDef SI4463_GetFifoStatus(SI4463_Context_t *ctx, uint8_t *rx_count,
                                        uint8_t *tx_count, uint8_t reset)
{
    uint8_t cmd[] = {SI4463_CMD_FIFO_INFO, 0x00};
    uint8_t response[2];
    HAL_StatusTypeDef status;
    
    cmd[1] = reset ? 0x03 : 0x00;
    
    status = SI4463_SendCommand(ctx, cmd, 2);
    if (status != HAL_OK) {
        return status;
    }
    
    status = SI4463_ReadCTS(ctx, response, 2, SI4463_CTS_TIMEOUT_SHORT);
    if (status != HAL_OK) {
        return status;
    }
    
    if (rx_count != NULL) {
        *rx_count = response[0];
    }
    if (tx_count != NULL) {
        *tx_count = response[1];
    }
    
    return HAL_OK;
}

/**
  * @brief  Write data to TX FIFO
  */
HAL_StatusTypeDef SI4463_WriteTxFifo(SI4463_Context_t *ctx, const uint8_t *data, uint8_t len)
{
    HAL_StatusTypeDef status;
    uint8_t cmd = SI4463_CMD_WRITE_TX_FIFO;
    uint8_t dummy_rx[130];
    
    if (ctx == NULL || data == NULL || len == 0 || len > 129) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus */
    if (SI4463_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    SI4463_CS_Low(ctx);
    
    /* Send FIFO write command */
    status = HAL_SPI_TransmitReceive(ctx->hspi, &cmd, dummy_rx, 1, SI4463_SPI_TIMEOUT);
    if (status == HAL_OK) {
        /* Send data */
        status = HAL_SPI_TransmitReceive(ctx->hspi, (uint8_t *)data, dummy_rx, len, SI4463_SPI_TIMEOUT);
    }
    
    SI4463_CS_High(ctx);
    
    /* Small delay */
    for (volatile int i = 0; i < 10; i++);
    
    SI4463_SPI_Unlock(ctx);
    
    return status;
}

/**
  * @brief  Read data from RX FIFO
  */
HAL_StatusTypeDef SI4463_ReadRxFifo(SI4463_Context_t *ctx, uint8_t *data, uint8_t len)
{
    HAL_StatusTypeDef status;
    uint8_t cmd = SI4463_CMD_READ_RX_FIFO;
    uint8_t dummy_tx[130] = {0};
    uint8_t dummy_rx;
    
    if (ctx == NULL || data == NULL || len == 0 || len > 129) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus */
    if (SI4463_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    SI4463_CS_Low(ctx);
    
    /* Send FIFO read command */
    status = HAL_SPI_TransmitReceive(ctx->hspi, &cmd, &dummy_rx, 1, SI4463_SPI_TIMEOUT);
    if (status == HAL_OK) {
        /* Receive data */
        status = HAL_SPI_TransmitReceive(ctx->hspi, dummy_tx, data, len, SI4463_SPI_TIMEOUT);
    }
    
    SI4463_CS_High(ctx);
    
    /* Small delay */
    for (volatile int i = 0; i < 10; i++);
    
    SI4463_SPI_Unlock(ctx);
    
    return status;
}

/**
  * @brief  Start RX mode
  */
HAL_StatusTypeDef SI4463_StartRx(SI4463_Context_t *ctx, uint8_t channel)
{
    uint8_t cmd[] = {SI4463_CMD_START_RX, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08};
    HAL_StatusTypeDef status;
    
    cmd[1] = channel;
    
    status = SI4463_SendCommand(ctx, cmd, 8);
    if (status != HAL_OK) {
        return status;
    }
    
    status = SI4463_ReadCTS(ctx, NULL, 0, SI4463_CTS_TIMEOUT_SHORT);
    
    if (status == HAL_OK) {
        ctx->rx_tx_state = SI4463_RXSTATE_RX;
    }
    
    return status;
}

/**
  * @brief  Start TX mode
  */
HAL_StatusTypeDef SI4463_StartTx(SI4463_Context_t *ctx, uint8_t channel, uint16_t size)
{
    uint8_t cmd[] = {SI4463_CMD_START_TX, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    cmd[1] = channel;
    cmd[3] = (size & 0x1F00) >> 8;
    cmd[4] = size & 0xFF;
    
    if (SI4463_SendCommand(ctx, cmd, 8) == HAL_OK) {
        ctx->rx_tx_state = SI4463_RXSTATE_TX;
        return HAL_OK;
    }
    
    return HAL_ERROR;
}

/**
  * @brief  Prepare SI4463 for TX transmission
  * @note   This switches radio to TX_TUNE state, resets FIFOs, and prepares for transmission
  */
HAL_StatusTypeDef SI4463_PrepareTX(SI4463_Context_t *ctx, uint8_t preamble_length)
{
    uint8_t rx_count, tx_count;
    
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    /* Switch to TX_TUNE state (0x05) */
    if (SI4463_ChangeState(ctx, 0x05) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Reset FIFOs */
    if (SI4463_GetFifoStatus(ctx, &rx_count, &tx_count, 1) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Set preamble length */
    if (SI4463_SetPreambleLength(ctx, preamble_length) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Clear interrupts */
    if (SI4463_ClearInterrupts(ctx, 0xFF, 0xFF) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Update internal state */
    ctx->rx_tx_state = SI4463_RXSTATE_PREP_TX;
    
    return HAL_OK;
}

/**
  * @brief  Return SI4463 from TX to RX mode
  * @note   Called after TX transmission completes
  */
HAL_StatusTypeDef SI4463_TxToRxTransition(SI4463_Context_t *ctx)
{
    uint8_t rx_count, tx_count;
    
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    /* Reset FIFOs */
    if (SI4463_GetFifoStatus(ctx, &rx_count, &tx_count, 1) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Clear interrupts */
    if (SI4463_ClearInterrupts(ctx, 0xFF, 0xFF) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Start RX */
    if (SI4463_StartRx(ctx, 0) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Update internal state */
    ctx->rx_tx_state = SI4463_RXSTATE_RX;
    
    return HAL_OK;
}

/**
  * @brief  Read Fast Response Registers
  */
HAL_StatusTypeDef SI4463_ReadFRR(SI4463_Context_t *ctx, uint8_t *data)
{
    HAL_StatusTypeDef status;
    uint8_t cmd = SI4463_CMD_FRR_A_READ;
    uint8_t rx_buf[5];
    
    if (ctx == NULL || data == NULL) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus */
    if (SI4463_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    SI4463_CS_Low(ctx);
    
    /* Read 5 bytes (cmd + 4 FRR registers) */
    status = HAL_SPI_TransmitReceive(ctx->hspi, &cmd, rx_buf, 5, SI4463_SPI_TIMEOUT);
    
    SI4463_CS_High(ctx);
    
    /* Small delay */
    for (volatile int i = 0; i < 10; i++);
    
    SI4463_SPI_Unlock(ctx);
    
    /* Copy FRR data (skip command echo) */
    if (status == HAL_OK) {
        for (int i = 0; i < 4; i++) {
            data[i] = rx_buf[i + 1];
        }
    }
    
    return status;
}

/**
  * @brief  Read chip temperature
  * @retval Temperature in °C, or 0xFFFF on error
  */
int16_t SI4463_ReadTemperature(SI4463_Context_t *ctx)
{
    uint8_t cmd[] = {SI4463_CMD_GET_ADC_READING, 0x10, 0xA0};
    uint8_t response[6];
    int16_t temperature;
    HAL_StatusTypeDef status;
    
    status = SI4463_SendCommand(ctx, cmd, 3);
    if (status != HAL_OK) {
        return 0xFFFF;
    }
    
    /* Small delay */
    for (volatile int i = 0; i < 200; i++);
    
    status = SI4463_ReadCTS(ctx, response, 6, SI4463_CTS_TIMEOUT_LONG);
    if (status != HAL_OK) {
        return 0xFFFF;
    }
    
    /* Calculate temperature from ADC reading */
    temperature = (response[4] << 8) | response[5];
    temperature = (int16_t)(temperature * 0.2195 - 293);
    
    return temperature;
}

/**
  * @brief  Check if temperature calibration is needed
  */
HAL_StatusTypeDef SI4463_CheckTemperatureCalibration(SI4463_Context_t *ctx, uint8_t *needs_recal)
{
    int16_t current_temp;
    int16_t delta_temp;
    
    if (ctx == NULL || needs_recal == NULL) {
        return HAL_ERROR;
    }
    
    current_temp = SI4463_ReadTemperature(ctx);
    
    if (current_temp == 0xFFFF) {
        return HAL_ERROR;
    }
    
    /* Initialize last temperature if not set */
    if (ctx->last_temperature == 300) {
        ctx->last_temperature = current_temp;
        *needs_recal = 0;
        return HAL_OK;
    }
    
    delta_temp = current_temp - ctx->last_temperature;
    
    /* Check if delta exceeds threshold */
    if (delta_temp > SI4463_TEMP_RECAL_THRESHOLD || delta_temp < -SI4463_TEMP_RECAL_THRESHOLD) {
        *needs_recal = 1;
        ctx->last_temperature = current_temp;
    } else {
        *needs_recal = 0;
    }
    
    return HAL_OK;
}

/* Private functions */

/**
  * @brief  Activate chip select (low)
  */
static inline void SI4463_CS_Low(SI4463_Context_t *ctx)
{
    HAL_GPIO_WritePin(ctx->cs_port, ctx->cs_pin, GPIO_PIN_RESET);
}

/**
  * @brief  Deactivate chip select (high)
  */
static inline void SI4463_CS_High(SI4463_Context_t *ctx)
{
    HAL_GPIO_WritePin(ctx->cs_port, ctx->cs_pin, GPIO_PIN_SET);
}

/**
  * @brief  Lock SPI bus mutex
  */
static HAL_StatusTypeDef SI4463_SPI_Lock(SI4463_Context_t *ctx)
{
    if (xSemaphoreTake(ctx->spi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        return HAL_OK;
    }
    return HAL_TIMEOUT;
}

/**
  * @brief  Unlock SPI bus mutex
  */
static void SI4463_SPI_Unlock(SI4463_Context_t *ctx)
{
    xSemaphoreGive(ctx->spi_mutex);
}

/************************ (C) COPYRIGHT NPR-70 FreeRTOS Port *****END OF FILE****/
