/**
  ******************************************************************************
  * @file    w5500_driver.c
  * @brief   W5500 Ethernet controller driver implementation
  * @note    Ported from mbed-based implementation to STM32 HAL
  ******************************************************************************
  * This file is part of NPR70 modem firmware
  * Copyright (c) 2017-2020 Guillaume F. F4HDK
  * Copyright (c) 2025 FreeRTOS port
  * Licensed under GPL v3
  ******************************************************************************
  */

#include "w5500_driver.h"
#include "FreeRTOS.h"
#include "task.h"

/* Private defines */
#define W5500_SPI_TIMEOUT       100  /* SPI timeout in ms */
#define W5500_READ_MODE         0x00  /* Variable length data mode, read */
#define W5500_WRITE_MODE        0x04  /* Variable length data mode, write */

/* Private function prototypes */
static inline void W5500_CS_Low(W5500_Context_t *ctx);
static inline void W5500_CS_High(W5500_Context_t *ctx);
static HAL_StatusTypeDef W5500_SPI_Lock(W5500_Context_t *ctx);
static void W5500_SPI_Unlock(W5500_Context_t *ctx);

/**
  * @brief  Initialize W5500 device
  */
HAL_StatusTypeDef W5500_Init(W5500_Context_t *ctx)
{
    if (ctx == NULL || ctx->hspi == NULL || ctx->spi_mutex == NULL) {
        return HAL_ERROR;
    }

    /* Ensure CS is high (inactive) */
    HAL_GPIO_WritePin(ctx->cs_port, ctx->cs_pin, GPIO_PIN_SET);
    
    /* Small delay for W5500 to stabilize */
    vTaskDelay(pdMS_TO_TICKS(10));
    
    /* Perform soft reset via PHY configuration */
    W5500_WriteByte(ctx, W5500_MR, W5500_COMMON_REG_BLOCK, 0x80);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    return HAL_OK;
}

/**
  * @brief  Read multiple bytes from W5500
  */
HAL_StatusTypeDef W5500_ReadData(W5500_Context_t *ctx, uint16_t addr, uint8_t block,
                                  uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status;
    uint8_t header[3];
    
    if (ctx == NULL || data == NULL || len == 0) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus (shared with SRAM) */
    if (W5500_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Prepare W5500 SPI frame header:
     * Byte 0-1: 16-bit address (MSB first)
     * Byte 2: Block select + control (read, variable length mode)
     */
    header[0] = (addr >> 8) & 0xFF;
    header[1] = addr & 0xFF;
    header[2] = (block << 3) | W5500_READ_MODE;
    
    /* Activate chip select */
    W5500_CS_Low(ctx);
    
    /* Send header */
    status = HAL_SPI_Transmit(ctx->hspi, header, 3, W5500_SPI_TIMEOUT);
    if (status != HAL_OK) {
        W5500_CS_High(ctx);
        W5500_SPI_Unlock(ctx);
        return status;
    }
    
    /* Receive data */
    status = HAL_SPI_Receive(ctx->hspi, data, len, W5500_SPI_TIMEOUT);
    
    /* Small delay before deactivating CS (W5500 datasheet requirement) */
    for (volatile int i = 0; i < 10; i++);
    
    /* Deactivate chip select */
    W5500_CS_High(ctx);
    
    /* Unlock SPI bus */
    W5500_SPI_Unlock(ctx);
    
    return status;
}

/**
  * @brief  Write multiple bytes to W5500
  */
HAL_StatusTypeDef W5500_WriteData(W5500_Context_t *ctx, uint16_t addr, uint8_t block,
                                   const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status;
    uint8_t header[3];
    
    if (ctx == NULL || data == NULL || len == 0) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus */
    if (W5500_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Prepare header */
    header[0] = (addr >> 8) & 0xFF;
    header[1] = addr & 0xFF;
    header[2] = (block << 3) | W5500_WRITE_MODE;
    
    /* Activate chip select */
    W5500_CS_Low(ctx);
    
    /* Send header */
    status = HAL_SPI_Transmit(ctx->hspi, header, 3, W5500_SPI_TIMEOUT);
    if (status != HAL_OK) {
        W5500_CS_High(ctx);
        W5500_SPI_Unlock(ctx);
        return status;
    }
    
    /* Send data */
    status = HAL_SPI_Transmit(ctx->hspi, (uint8_t *)data, len, W5500_SPI_TIMEOUT);
    
    /* Small delay before deactivating CS */
    for (volatile int i = 0; i < 10; i++);
    
    /* Deactivate chip select */
    W5500_CS_High(ctx);
    
    /* Unlock SPI bus */
    W5500_SPI_Unlock(ctx);
    
    return status;
}

/**
  * @brief  Read single byte from W5500
  */
uint8_t W5500_ReadByte(W5500_Context_t *ctx, uint16_t addr, uint8_t block)
{
    uint8_t data = 0;
    W5500_ReadData(ctx, addr, block, &data, 1);
    return data;
}

/**
  * @brief  Write single byte to W5500
  */
HAL_StatusTypeDef W5500_WriteByte(W5500_Context_t *ctx, uint16_t addr, uint8_t block, uint8_t data)
{
    return W5500_WriteData(ctx, addr, block, &data, 1);
}

/**
  * @brief  Read 16-bit word from W5500
  */
uint16_t W5500_ReadWord(W5500_Context_t *ctx, uint16_t addr, uint8_t block)
{
    uint8_t data[2];
    W5500_ReadData(ctx, addr, block, data, 2);
    return ((uint16_t)data[0] << 8) | data[1];
}

/**
  * @brief  Get received data size for a socket
  * @note   Read twice to ensure stable value (W5500 requirement)
  */
uint16_t W5500_GetRxSize(W5500_Context_t *ctx, uint8_t sock)
{
    uint16_t size = 0;
    uint16_t prev_size = 0;
    
    /* Read until we get two consistent values */
    do {
        prev_size = size;
        size = W5500_ReadWord(ctx, W5500_Sn_RX_RSR0, W5500_SOCKET_REG_BLOCK(sock));
    } while (prev_size != size);
    
    return size;
}

/**
  * @brief  Get free TX buffer size for a socket
  * @note   Read twice to ensure stable value
  */
uint16_t W5500_GetTxFreeSize(W5500_Context_t *ctx, uint8_t sock)
{
    uint16_t size = 0;
    uint16_t prev_size = 0;
    
    /* Read until we get two consistent values */
    do {
        prev_size = size;
        size = W5500_ReadWord(ctx, W5500_Sn_TX_FSR0, W5500_SOCKET_REG_BLOCK(sock));
    } while (prev_size != size);
    
    return size;
}

/**
  * @brief  Read from socket RX buffer
  */
HAL_StatusTypeDef W5500_RecvData(W5500_Context_t *ctx, uint8_t sock, uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status;
    uint16_t read_ptr;
    uint8_t ptr_buf[2];
    
    if (ctx == NULL || data == NULL || len == 0) {
        return HAL_ERROR;
    }
    
    /* Get current RX read pointer */
    read_ptr = W5500_ReadWord(ctx, W5500_Sn_RX_RD0, W5500_SOCKET_REG_BLOCK(sock));
    
    /* Read data from RX buffer */
    status = W5500_ReadData(ctx, read_ptr, W5500_RX_BUFFER_BLOCK(sock), data, len);
    if (status != HAL_OK) {
        return status;
    }
    
    /* Update RX read pointer */
    read_ptr += len;
    ptr_buf[0] = (read_ptr >> 8) & 0xFF;
    ptr_buf[1] = read_ptr & 0xFF;
    status = W5500_WriteData(ctx, W5500_Sn_RX_RD0, W5500_SOCKET_REG_BLOCK(sock), ptr_buf, 2);
    if (status != HAL_OK) {
        return status;
    }
    
    /* Execute RECV command to complete the operation */
    return W5500_ExecCommand(ctx, sock, W5500_Sn_CR_RECV);
}

/**
  * @brief  Write to socket TX buffer
  */
HAL_StatusTypeDef W5500_SendData(W5500_Context_t *ctx, uint8_t sock, const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status;
    uint16_t write_ptr;
    uint8_t ptr_buf[2];
    
    if (ctx == NULL || data == NULL || len == 0) {
        return HAL_ERROR;
    }
    
    /* Get current TX write pointer */
    write_ptr = W5500_ReadWord(ctx, W5500_Sn_TX_WR0, W5500_SOCKET_REG_BLOCK(sock));
    
    /* Write data to TX buffer */
    status = W5500_WriteData(ctx, write_ptr, W5500_TX_BUFFER_BLOCK(sock), data, len);
    if (status != HAL_OK) {
        return status;
    }
    
    /* Update TX write pointer */
    write_ptr += len;
    ptr_buf[0] = (write_ptr >> 8) & 0xFF;
    ptr_buf[1] = write_ptr & 0xFF;
    status = W5500_WriteData(ctx, W5500_Sn_TX_WR0, W5500_SOCKET_REG_BLOCK(sock), ptr_buf, 2);
    if (status != HAL_OK) {
        return status;
    }
    
    /* Execute SEND command to transmit */
    return W5500_ExecCommand(ctx, sock, W5500_Sn_CR_SEND);
}

/**
  * @brief  Execute socket command
  */
HAL_StatusTypeDef W5500_ExecCommand(W5500_Context_t *ctx, uint8_t sock, uint8_t cmd)
{
    HAL_StatusTypeDef status;
    uint32_t timeout = 1000; /* 1000 iterations timeout */
    
    /* Write command to socket command register */
    status = W5500_WriteByte(ctx, W5500_Sn_CR, W5500_SOCKET_REG_BLOCK(sock), cmd);
    if (status != HAL_OK) {
        return status;
    }
    
    /* Wait for command to complete (register returns to 0) */
    while (W5500_ReadByte(ctx, W5500_Sn_CR, W5500_SOCKET_REG_BLOCK(sock)) != 0) {
        if (--timeout == 0) {
            return HAL_TIMEOUT;
        }
        /* Small delay to avoid hammering the SPI bus */
        for (volatile int i = 0; i < 100; i++);
    }
    
    return HAL_OK;
}

/**
  * @brief  Get socket status
  */
uint8_t W5500_GetSocketStatus(W5500_Context_t *ctx, uint8_t sock)
{
    return W5500_ReadByte(ctx, W5500_Sn_SR, W5500_SOCKET_REG_BLOCK(sock));
}

/**
  * @brief  PHY power off for 5 seconds (reset)
  */
HAL_StatusTypeDef W5500_PhyReset(W5500_Context_t *ctx)
{
    HAL_StatusTypeDef status;
    
    /* Power down PHY */
    status = W5500_WriteByte(ctx, W5500_PHYCFGR, W5500_COMMON_REG_BLOCK, 0x70);
    if (status != HAL_OK) {
        return status;
    }
    
    /* Wait 5 seconds */
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    /* Power up PHY */
    status = W5500_WriteByte(ctx, W5500_PHYCFGR, W5500_COMMON_REG_BLOCK, 0xF8);
    
    return status;
}

/* Private functions */

/**
  * @brief  Activate chip select (low)
  */
static inline void W5500_CS_Low(W5500_Context_t *ctx)
{
    HAL_GPIO_WritePin(ctx->cs_port, ctx->cs_pin, GPIO_PIN_RESET);
}

/**
  * @brief  Deactivate chip select (high)
  */
static inline void W5500_CS_High(W5500_Context_t *ctx)
{
    HAL_GPIO_WritePin(ctx->cs_port, ctx->cs_pin, GPIO_PIN_SET);
}

/**
  * @brief  Lock SPI bus mutex
  */
static HAL_StatusTypeDef W5500_SPI_Lock(W5500_Context_t *ctx)
{
    if (xSemaphoreTake(ctx->spi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        return HAL_OK;
    }
    return HAL_TIMEOUT;
}

/**
  * @brief  Unlock SPI bus mutex
  */
static void W5500_SPI_Unlock(W5500_Context_t *ctx)
{
    xSemaphoreGive(ctx->spi_mutex);
}

/**
  * @brief  Initialize UDP socket
  */
HAL_StatusTypeDef W5500_InitUDPSocket(W5500_Context_t *ctx, uint8_t sock, uint16_t port)
{
    if (ctx == NULL || sock > 7) {
        return HAL_ERROR;
    }
    
    uint8_t block = W5500_SOCKET_REG_BLOCK(sock);
    
    /* Set socket mode to UDP */
    W5500_WriteByte(ctx, W5500_Sn_MR, block, W5500_Sn_MR_UDP);
    
    /* Set source port */
    W5500_WriteByte(ctx, W5500_Sn_PORT0, block, (port >> 8) & 0xFF);
    W5500_WriteByte(ctx, W5500_Sn_PORT0 + 1, block, port & 0xFF);
    
    /* Set buffer sizes (2KB TX, 2KB RX) */
    W5500_WriteByte(ctx, W5500_Sn_TXBUF_SIZE, block, 0x02);
    W5500_WriteByte(ctx, W5500_Sn_RXBUF_SIZE, block, 0x02);
    
    /* Open socket */
    W5500_WriteByte(ctx, W5500_Sn_CR, block, W5500_Sn_CR_OPEN);
    
    /* Wait for socket to open */
    vTaskDelay(pdMS_TO_TICKS(10));
    
    return HAL_OK;
}

/**
  * @brief  Initialize TCP server socket
  */
HAL_StatusTypeDef W5500_InitTCPServerSocket(W5500_Context_t *ctx, uint8_t sock, uint16_t port)
{
    if (ctx == NULL || sock > 7) {
        return HAL_ERROR;
    }
    
    uint8_t block = W5500_SOCKET_REG_BLOCK(sock);
    
    /* Set socket mode to TCP */
    W5500_WriteByte(ctx, W5500_Sn_MR, block, W5500_Sn_MR_TCP);
    
    /* Set source port */
    W5500_WriteByte(ctx, W5500_Sn_PORT0, block, (port >> 8) & 0xFF);
    W5500_WriteByte(ctx, W5500_Sn_PORT0 + 1, block, port & 0xFF);
    
    /* Set buffer sizes (2KB TX, 2KB RX) */
    W5500_WriteByte(ctx, W5500_Sn_TXBUF_SIZE, block, 0x02);
    W5500_WriteByte(ctx, W5500_Sn_RXBUF_SIZE, block, 0x02);
    
    /* Open socket */
    W5500_WriteByte(ctx, W5500_Sn_CR, block, W5500_Sn_CR_OPEN);
    
    /* Wait for socket to initialize */
    vTaskDelay(pdMS_TO_TICKS(10));
    
    /* Put socket in listen mode */
    W5500_WriteByte(ctx, W5500_Sn_CR, block, W5500_Sn_CR_LISTEN);
    
    /* Wait for listen state */
    vTaskDelay(pdMS_TO_TICKS(10));
    
    return HAL_OK;
}

/**
  * @brief  Configure all application sockets
  */
HAL_StatusTypeDef W5500_ConfigureAppSockets(W5500_Context_t *ctx)
{
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    /* Socket 0: DHCP Server (UDP port 67) */
    if (W5500_InitUDPSocket(ctx, W5500_SOCK_DHCP, 67) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Socket 3: SNMP Agent (UDP port 161) */
    if (W5500_InitUDPSocket(ctx, W5500_SOCK_SNMP, 161) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Socket 4: Telnet Server (TCP port 23) */
    if (W5500_InitTCPServerSocket(ctx, W5500_SOCK_TELNET, 23) != HAL_OK) {
        return HAL_ERROR;
    }
    
    return HAL_OK;
}

/************************ (C) COPYRIGHT NPR-70 FreeRTOS Port *****END OF FILE****/
