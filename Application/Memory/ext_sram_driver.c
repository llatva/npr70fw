/**
  ******************************************************************************
  * @file    ext_sram_driver.c
  * @brief   External SPI SRAM driver implementation
  * @note    Ported from mbed-based implementation to STM32 HAL
  ******************************************************************************
  * This file is part of NPR70 modem firmware
  * Copyright (c) 2017-2020 Guillaume F. F4HDK
  * Copyright (c) 2025 FreeRTOS port
  * Licensed under GPL v3
  ******************************************************************************
  */

#include "ext_sram_driver.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* Use TIM2 microsecond counter for delays before scheduler if available */
extern TIM_HandleTypeDef htim2;

/* Private defines */
#define SRAM_SPI_TIMEOUT        100  /* SPI timeout in ms */

/* Private function prototypes */
static inline void ExtSRAM_CS_Low(ExtSRAM_Context_t *ctx);
static inline void ExtSRAM_CS_High(ExtSRAM_Context_t *ctx);
static HAL_StatusTypeDef ExtSRAM_SPI_Lock(ExtSRAM_Context_t *ctx);
static void ExtSRAM_SPI_Unlock(ExtSRAM_Context_t *ctx);
static uint16_t ExtSRAM_FIFO_FindFreeSlot(ExtSRAM_FIFO_Mgmt_t *mgmt);

/**
  * @brief  Initialize external SRAM device
  * @note   Called before scheduler starts, so cannot use mutex/semaphore
  */
HAL_StatusTypeDef ExtSRAM_Init(ExtSRAM_Context_t *ctx)
{
    HAL_StatusTypeDef status;
    uint8_t cmd[2];
    uint8_t dummy_rx[2];
    
    if (ctx == NULL || ctx->hspi == NULL || ctx->spi_mutex == NULL) {
        printf("ExtSRAM_Init: invalid ctx or handles (ctx=%p, hspi=%p, mutex=%p)\r\n", (void*)ctx, (void*)ctx->hspi, (void*)ctx->spi_mutex);
        return HAL_ERROR;
    }

    /* Ensure CS is high (inactive) */
    printf("ExtSRAM_Init: setting CS high\r\n");
    HAL_GPIO_WritePin(ctx->cs_port, ctx->cs_pin, GPIO_PIN_SET);
    
    /* Small delay for SRAM to stabilize. HAL_Delay may not work before scheduler
       so use TIM2 microsecond counter if available (TIM2 configured at 1MHz). */
    printf("ExtSRAM_Init: busy-waiting 10ms using TIM2\r\n");
    if (&htim2 != NULL && htim2.Instance != NULL) {
        uint32_t start = __HAL_TIM_GET_COUNTER(&htim2);
        while (((uint32_t)(__HAL_TIM_GET_COUNTER(&htim2) - start)) < 10000U) {
            /* busy wait ~10ms */
        }
    } else {
        /* Fallback to simple loop (approximate) */
        for (volatile uint32_t i = 0; i < 160000; i++); /* rough ~10ms at 80MHz CPU */
    }
    printf("ExtSRAM_Init: delay done\r\n");
    
    /* Set SRAM to sequential mode via direct SPI (no mutex - scheduler not running yet) */
    cmd[0] = SRAM_CMD_WRMR;
    cmd[1] = SRAM_MODE_SEQUENTIAL;
    
    printf("ExtSRAM_Init: pulling CS low and sending WRMR\r\n");
    ExtSRAM_CS_Low(ctx);
    status = HAL_SPI_TransmitReceive(ctx->hspi, cmd, dummy_rx, 2, SRAM_SPI_TIMEOUT);
    printf("ExtSRAM_Init: HAL_SPI_TransmitReceive returned %d\r\n", (int)status);
    ExtSRAM_CS_High(ctx);
    printf("ExtSRAM_Init: CS high after WRMR\r\n");
    
    return status;
}

/**
  * @brief  Set SRAM operating mode
  */
HAL_StatusTypeDef ExtSRAM_SetMode(ExtSRAM_Context_t *ctx, uint8_t mode)
{
    HAL_StatusTypeDef status;
    uint8_t cmd[2];
    uint8_t dummy_rx[2];
    
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus (shared with W5500) */
    if (ExtSRAM_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    cmd[0] = SRAM_CMD_WRMR;
    cmd[1] = mode;
    
    ExtSRAM_CS_Low(ctx);
    status = HAL_SPI_TransmitReceive(ctx->hspi, cmd, dummy_rx, 2, SRAM_SPI_TIMEOUT);
    ExtSRAM_CS_High(ctx);
    
    ExtSRAM_SPI_Unlock(ctx);
    
    return status;
}

/**
  * @brief  Read data from external SRAM
  */
HAL_StatusTypeDef ExtSRAM_Read(ExtSRAM_Context_t *ctx, uint8_t *data, uint32_t address, uint16_t size)
{
    HAL_StatusTypeDef status;
    uint8_t cmd[4];
    uint8_t dummy_rx[4];
    uint8_t dummy_tx[512] = {0};
    
    if (ctx == NULL || data == NULL || size == 0) {
        return HAL_ERROR;
    }
    
    /* Check address bounds */
    if ((address + size) > SRAM_MAX_SIZE) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus */
    if (ExtSRAM_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Prepare READ command with 24-bit address */
    cmd[0] = SRAM_CMD_READ;
    cmd[1] = (address >> 16) & 0xFF;
    cmd[2] = (address >> 8) & 0xFF;
    cmd[3] = address & 0xFF;
    
    ExtSRAM_CS_Low(ctx);
    
    /* Send command and address */
    status = HAL_SPI_TransmitReceive(ctx->hspi, cmd, dummy_rx, 4, SRAM_SPI_TIMEOUT);
    if (status != HAL_OK) {
        ExtSRAM_CS_High(ctx);
        ExtSRAM_SPI_Unlock(ctx);
        return status;
    }
    
    /* Read data (in chunks if needed for large transfers) */
    uint16_t remaining = size;
    uint16_t offset = 0;
    while (remaining > 0 && status == HAL_OK) {
        uint16_t chunk_size = (remaining > 512) ? 512 : remaining;
        status = HAL_SPI_TransmitReceive(ctx->hspi, dummy_tx, data + offset, chunk_size, SRAM_SPI_TIMEOUT);
        offset += chunk_size;
        remaining -= chunk_size;
    }
    
    ExtSRAM_CS_High(ctx);
    ExtSRAM_SPI_Unlock(ctx);
    
    return status;
}

/**
  * @brief  Write data to external SRAM
  */
HAL_StatusTypeDef ExtSRAM_Write(ExtSRAM_Context_t *ctx, const uint8_t *data, uint32_t address, uint16_t size)
{
    HAL_StatusTypeDef status;
    uint8_t cmd[4];
    uint8_t dummy_rx[4];
    uint8_t dummy_rx_data[512];
    
    if (ctx == NULL || data == NULL || size == 0) {
        return HAL_ERROR;
    }
    
    /* Check address bounds */
    if ((address + size) > SRAM_MAX_SIZE) {
        return HAL_ERROR;
    }
    
    /* Lock SPI bus */
    if (ExtSRAM_SPI_Lock(ctx) != HAL_OK) {
        return HAL_ERROR;
    }
    
    /* Prepare WRITE command with 24-bit address */
    cmd[0] = SRAM_CMD_WRITE;
    cmd[1] = (address >> 16) & 0xFF;
    cmd[2] = (address >> 8) & 0xFF;
    cmd[3] = address & 0xFF;
    
    ExtSRAM_CS_Low(ctx);
    
    /* Send command and address */
    status = HAL_SPI_TransmitReceive(ctx->hspi, cmd, dummy_rx, 4, SRAM_SPI_TIMEOUT);
    if (status != HAL_OK) {
        ExtSRAM_CS_High(ctx);
        ExtSRAM_SPI_Unlock(ctx);
        return status;
    }
    
    /* Write data (in chunks if needed for large transfers) */
    uint16_t remaining = size;
    uint16_t offset = 0;
    while (remaining > 0 && status == HAL_OK) {
        uint16_t chunk_size = (remaining > 512) ? 512 : remaining;
        status = HAL_SPI_TransmitReceive(ctx->hspi, (uint8_t *)(data + offset), dummy_rx_data, 
                                          chunk_size, SRAM_SPI_TIMEOUT);
        offset += chunk_size;
        remaining -= chunk_size;
    }
    
    ExtSRAM_CS_High(ctx);
    ExtSRAM_SPI_Unlock(ctx);
    
    return status;
}

/**
  * @brief  Initialize FIFO management structure
  */
void ExtSRAM_FIFO_Init(ExtSRAM_FIFO_Mgmt_t *mgmt)
{
    if (mgmt == NULL) {
        return;
    }
    
    /* Clear all FIFO arrays and counters */
    memset(mgmt, 0, sizeof(ExtSRAM_FIFO_Mgmt_t));
}

/**
  * @brief  Test if there's free space for packets
  * @param  pkt_count Number of packets to check space for
  * @param  fifo_idx FIFO index to check
  * @retval 1 if space available, 0 if not
  */
uint8_t ExtSRAM_FIFO_TestFreeSpace(ExtSRAM_FIFO_Mgmt_t *mgmt, uint8_t pkt_count, uint8_t fifo_idx)
{
    if (mgmt == NULL || fifo_idx >= SRAM_FIFO_COUNT) {
        return 0;
    }
    
    /* Check FIFO-specific space */
    if ((mgmt->fifo_filling[fifo_idx] + pkt_count) > SRAM_FIFO_DEPTH) {
        return 0;
    }
    
    /* Check total SRAM space */
    if ((mgmt->total_filling + pkt_count) > SRAM_MAX_FRAMES) {
        return 0;
    }
    
    return 1;
}

/**
  * @brief  Find first free slot in SRAM
  * @retval Slot index, or 0xFFFF if no free slot
  */
static uint16_t ExtSRAM_FIFO_FindFreeSlot(ExtSRAM_FIFO_Mgmt_t *mgmt)
{
    for (uint16_t i = 0; i < SRAM_MAX_FRAMES; i++) {
        if (mgmt->filling[i] == 0) {
            return i;
        }
    }
    return 0xFFFF;
}

/**
  * @brief  Push data to SRAM FIFO
  */
HAL_StatusTypeDef ExtSRAM_FIFO_Push(ExtSRAM_Context_t *ctx, ExtSRAM_FIFO_Mgmt_t *mgmt,
                                     const uint8_t *data, uint16_t size, uint8_t fifo_nb, uint8_t timestamp)
{
    HAL_StatusTypeDef status;
    uint16_t free_slot;
    uint32_t sram_address;
    
    if (ctx == NULL || mgmt == NULL || data == NULL || fifo_nb >= SRAM_FIFO_COUNT) {
        return HAL_ERROR;
    }
    
    if (size > SRAM_FRAME_SIZE) {
        return HAL_ERROR;
    }
    
    /* Find free slot */
    free_slot = ExtSRAM_FIFO_FindFreeSlot(mgmt);
    if (free_slot == 0xFFFF) {
        return HAL_ERROR;  /* No free space */
    }
    
    /* Mark slot as occupied */
    mgmt->filling[free_slot] = 1;
    
    /* Add to FIFO */
    mgmt->fifos[fifo_nb][mgmt->fifo_write_idx[fifo_nb]] = free_slot;
    mgmt->fifo_write_idx[fifo_nb]++;
    if (mgmt->fifo_write_idx[fifo_nb] >= SRAM_FIFO_DEPTH) {
        mgmt->fifo_write_idx[fifo_nb] = 0;
    }
    
    /* Update counters */
    mgmt->total_filling++;
    mgmt->fifo_filling[fifo_nb]++;
    
    /* Store metadata */
    mgmt->pkt_timer[free_slot] = timestamp;
    mgmt->pkt_size[free_slot] = size;
    
    /* Write to SRAM */
    sram_address = free_slot * SRAM_FRAME_SIZE;
    status = ExtSRAM_Write(ctx, data, sram_address, size);
    
    return status;
}

/**
  * @brief  Pop data from SRAM FIFO
  */
HAL_StatusTypeDef ExtSRAM_FIFO_Pop(ExtSRAM_Context_t *ctx, ExtSRAM_FIFO_Mgmt_t *mgmt,
                                    uint8_t *data, uint16_t *size, uint8_t fifo_nb)
{
    HAL_StatusTypeDef status;
    uint16_t slot_idx;
    uint32_t sram_address;
    
    if (ctx == NULL || mgmt == NULL || data == NULL || size == NULL || fifo_nb >= SRAM_FIFO_COUNT) {
        return HAL_ERROR;
    }
    
    /* Check if FIFO is empty */
    if (mgmt->fifo_filling[fifo_nb] == 0) {
        return HAL_ERROR;
    }
    
    /* Get slot from FIFO */
    slot_idx = mgmt->fifos[fifo_nb][mgmt->fifo_read_idx[fifo_nb]];
    
    /* Read from SRAM */
    sram_address = slot_idx * SRAM_FRAME_SIZE;
    *size = mgmt->pkt_size[slot_idx];
    status = ExtSRAM_Read(ctx, data, sram_address, *size);
    
    /* Free slot */
    mgmt->filling[slot_idx] = 0;
    
    /* Update FIFO pointers */
    mgmt->fifo_read_idx[fifo_nb]++;
    if (mgmt->fifo_read_idx[fifo_nb] >= SRAM_FIFO_DEPTH) {
        mgmt->fifo_read_idx[fifo_nb] = 0;
    }
    
    /* Update counters */
    mgmt->fifo_filling[fifo_nb]--;
    mgmt->total_filling--;
    
    return status;
}

/**
  * @brief  Get number of frames in a FIFO
  */
uint16_t ExtSRAM_FIFO_GetFilling(ExtSRAM_FIFO_Mgmt_t *mgmt, uint8_t fifo_nb)
{
    if (mgmt == NULL || fifo_nb >= SRAM_FIFO_COUNT) {
        return 0;
    }
    
    return mgmt->fifo_filling[fifo_nb];
}

/**
  * @brief  Get total number of frames stored
  */
uint16_t ExtSRAM_FIFO_GetTotalFilling(ExtSRAM_FIFO_Mgmt_t *mgmt)
{
    if (mgmt == NULL) {
        return 0;
    }
    
    return mgmt->total_filling;
}

/**
  * @brief  Test external SRAM read/write functionality
  */
HAL_StatusTypeDef ExtSRAM_Test(ExtSRAM_Context_t *ctx)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint8_t write_data[32] = "SRAM Test Data - NPR70";
    uint8_t read_data[32] = {0};
    uint32_t test_address = 0x1000;
    
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    /* Write test data */
    status = ExtSRAM_Write(ctx, write_data, test_address, sizeof(write_data));
    if (status != HAL_OK) {
        return status;
    }
    
    /* Small delay */
    vTaskDelay(pdMS_TO_TICKS(10));
    
    /* Read back test data */
    status = ExtSRAM_Read(ctx, read_data, test_address, sizeof(read_data));
    if (status != HAL_OK) {
        return status;
    }
    
    /* Verify data */
    for (uint8_t i = 0; i < sizeof(write_data); i++) {
        if (write_data[i] != read_data[i]) {
            return HAL_ERROR;
        }
    }
    
    return HAL_OK;
}

/* Private functions */

/**
  * @brief  Activate chip select (low)
  */
static inline void ExtSRAM_CS_Low(ExtSRAM_Context_t *ctx)
{
    HAL_GPIO_WritePin(ctx->cs_port, ctx->cs_pin, GPIO_PIN_RESET);
}

/**
  * @brief  Deactivate chip select (high)
  */
static inline void ExtSRAM_CS_High(ExtSRAM_Context_t *ctx)
{
    HAL_GPIO_WritePin(ctx->cs_port, ctx->cs_pin, GPIO_PIN_SET);
}

/**
  * @brief  Lock SPI bus mutex
  */
static HAL_StatusTypeDef ExtSRAM_SPI_Lock(ExtSRAM_Context_t *ctx)
{
    if (xSemaphoreTake(ctx->spi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        return HAL_OK;
    }
    return HAL_TIMEOUT;
}

/**
  * @brief  Unlock SPI bus mutex
  */
static void ExtSRAM_SPI_Unlock(ExtSRAM_Context_t *ctx)
{
    xSemaphoreGive(ctx->spi_mutex);
}
