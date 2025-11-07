/**
  ******************************************************************************
  * @file    ext_sram_driver.h
  * @brief   External SPI SRAM driver header
  * @note    Ported from mbed-based implementation to STM32 HAL
  ******************************************************************************
  * This file is part of NPR70 modem firmware
  * Copyright (c) 2017-2020 Guillaume F. F4HDK
  * Copyright (c) 2025 FreeRTOS port
  * Licensed under GPL v3
  ******************************************************************************
  */

#ifndef EXT_SRAM_DRIVER_H
#define EXT_SRAM_DRIVER_H

#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* SRAM Commands (23LC1024 or similar) */
#define SRAM_CMD_READ           0x03  /* Read data from memory */
#define SRAM_CMD_WRITE          0x02  /* Write data to memory */
#define SRAM_CMD_RDMR           0x05  /* Read Mode Register */
#define SRAM_CMD_WRMR           0x01  /* Write Mode Register */

/* SRAM Mode Register values */
#define SRAM_MODE_BYTE          0x00  /* Byte mode (default) */
#define SRAM_MODE_PAGE          0x80  /* Page mode */
#define SRAM_MODE_SEQUENTIAL    0x40  /* Sequential mode */

/* SRAM Configuration */
#define SRAM_MAX_SIZE           131072  /* 128KB (23LC1024) */
#define SRAM_FRAME_SIZE         350     /* Frame size in bytes */
#define SRAM_MAX_FRAMES         374     /* Maximum number of frames storable */
#define SRAM_FIFO_COUNT         8       /* Number of FIFOs */
#define SRAM_FIFO_DEPTH         94      /* Maximum frames per FIFO */

/**
  * @brief  External SRAM context structure
  */
typedef struct {
    SPI_HandleTypeDef *hspi;        /* SPI handle */
    GPIO_TypeDef *cs_port;          /* Chip select port */
    uint16_t cs_pin;                /* Chip select pin */
    SemaphoreHandle_t spi_mutex;    /* SPI bus mutex (shared with W5500) */
} ExtSRAM_Context_t;

/**
  * @brief  SRAM FIFO management structure
  */
typedef struct {
    uint16_t fifos[SRAM_FIFO_COUNT][SRAM_FIFO_DEPTH];  /* FIFO arrays storing frame indices */
    uint8_t filling[SRAM_MAX_FRAMES];                  /* Frame slot occupancy (0=empty, 1=occupied) */
    uint8_t pkt_timer[SRAM_MAX_FRAMES];                /* Frame timestamp (for timeout checking) */
    uint16_t pkt_size[SRAM_MAX_FRAMES];                /* Frame sizes */
    uint8_t fifo_read_idx[SRAM_FIFO_COUNT];            /* Read pointer for each FIFO */
    uint8_t fifo_write_idx[SRAM_FIFO_COUNT];           /* Write pointer for each FIFO */
    uint8_t fifo_filling[SRAM_FIFO_COUNT];             /* Number of frames in each FIFO */
    uint16_t total_filling;                            /* Total frames stored */
} ExtSRAM_FIFO_Mgmt_t;

/* Function prototypes - Low level */
HAL_StatusTypeDef ExtSRAM_Init(ExtSRAM_Context_t *ctx);
HAL_StatusTypeDef ExtSRAM_SetMode(ExtSRAM_Context_t *ctx, uint8_t mode);
HAL_StatusTypeDef ExtSRAM_Read(ExtSRAM_Context_t *ctx, uint8_t *data, uint32_t address, uint16_t size);
HAL_StatusTypeDef ExtSRAM_Write(ExtSRAM_Context_t *ctx, const uint8_t *data, uint32_t address, uint16_t size);

/* Function prototypes - FIFO management */
void ExtSRAM_FIFO_Init(ExtSRAM_FIFO_Mgmt_t *mgmt);
uint8_t ExtSRAM_FIFO_TestFreeSpace(ExtSRAM_FIFO_Mgmt_t *mgmt, uint8_t pkt_count, uint8_t fifo_idx);
HAL_StatusTypeDef ExtSRAM_FIFO_Push(ExtSRAM_Context_t *ctx, ExtSRAM_FIFO_Mgmt_t *mgmt,
                                     const uint8_t *data, uint16_t size, uint8_t fifo_nb, uint8_t timestamp);
HAL_StatusTypeDef ExtSRAM_FIFO_Pop(ExtSRAM_Context_t *ctx, ExtSRAM_FIFO_Mgmt_t *mgmt,
                                    uint8_t *data, uint16_t *size, uint8_t fifo_nb);
uint16_t ExtSRAM_FIFO_GetFilling(ExtSRAM_FIFO_Mgmt_t *mgmt, uint8_t fifo_nb);
uint16_t ExtSRAM_FIFO_GetTotalFilling(ExtSRAM_FIFO_Mgmt_t *mgmt);

/* Function prototypes - Testing */
HAL_StatusTypeDef ExtSRAM_Test(ExtSRAM_Context_t *ctx);

#endif /* EXT_SRAM_DRIVER_H */

/************************ (C) COPYRIGHT NPR-70 FreeRTOS Port *****END OF FILE****/
