/**
 ******************************************************************************
 * @file           : main.h
 * @brief          : Header for main.c file - NPR-70 FreeRTOS Port
 *                   STM32L432KC Cortex-M4F @ 80MHz
 ******************************************************************************
 * NPR-70 Modem Firmware - FreeRTOS Port
 * Copyright (c) 2017-2025 Guillaume F. F4HDK
 * FreeRTOS Port by Lasse OH3HZB 2025
 *
 * This is a complete rewrite using:
 * - FreeRTOS 11.x LTS
 * - STM32 HAL/LL drivers  
 * - No mbed dependencies
 ******************************************************************************
 */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "timers.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* Memory Section Attributes -------------------------------------------------*/
/**
 * @brief Place variable in SRAM2 (16KB at 0x10000000)
 * SRAM2 is separate from main SRAM1, ideal for large buffers to save SRAM1 space.
 * Use this for large static buffers in tasks to free up SRAM1.
 * Example: static uint8_t buffer[1024] PLACE_IN_SRAM2;
 */
#define PLACE_IN_SRAM2 __attribute__((section(".sram2")))

/* Exported types ------------------------------------------------------------*/

/* System event group bits */
#define EVENT_RADIO_CONFIGURED      (1 << 0)
#define EVENT_ETHERNET_CONFIGURED   (1 << 1)
#define EVENT_SYSTEM_READY          (1 << 2)
#define EVENT_RADIO_ON              (1 << 3)
#define EVENT_ETHERNET_LINK_UP      (1 << 4)
#define EVENT_TDMA_MASTER           (1 << 5)
#define EVENT_TDMA_CONNECTED        (1 << 6)

/* Firmware version */
#define FW_VERSION "NPR-FreeRTOS-fw-1.8.0"

/* Hardware Pin Definitions for STM32L432KC */
/* SPI1 - SI4463 Radio */
#define SI4463_SPI                  SPI1
#define SI4463_SPI_SCK_PIN          GPIO_PIN_5
#define SI4463_SPI_SCK_PORT         GPIOA
#define SI4463_SPI_MISO_PIN         GPIO_PIN_6
#define SI4463_SPI_MISO_PORT        GPIOA
#define SI4463_SPI_MOSI_PIN         GPIO_PIN_7
#define SI4463_SPI_MOSI_PORT        GPIOA
#define SI4463_CS_PIN               GPIO_PIN_4
#define SI4463_CS_PORT              GPIOA
#define SI4463_SDN_PIN              GPIO_PIN_1
#define SI4463_SDN_PORT             GPIOA
#define SI4463_INT_PIN              GPIO_PIN_3
#define SI4463_INT_PORT             GPIOA
#define SI4463_INT_EXTI_IRQn        EXTI3_IRQn

/* SPI2 - W5500 Ethernet & External SRAM */
#define W5500_SPI                   SPI2
#define W5500_SPI_SCK_PIN           GPIO_PIN_3
#define W5500_SPI_SCK_PORT          GPIOB
#define W5500_SPI_MISO_PIN          GPIO_PIN_4
#define W5500_SPI_MISO_PORT         GPIOB
#define W5500_SPI_MOSI_PIN          GPIO_PIN_5
#define W5500_SPI_MOSI_PORT         GPIOB
#define W5500_CS_PIN                GPIO_PIN_11
#define W5500_CS_PORT               GPIOA
#define W5500_INT_PIN               GPIO_PIN_8
#define W5500_INT_PORT              GPIOA
#define W5500_INT_EXTI_IRQn         EXTI9_5_IRQn

/* External SRAM (shares SPI2 with W5500) */
#define EXT_SRAM_CS_PIN             GPIO_PIN_0
#define EXT_SRAM_CS_PORT            GPIOB

/* Radio Control Pins */
#define PTT_PA_PIN                  GPIO_PIN_9
#define PTT_PA_PORT                 GPIOA
#define FDD_TRIG_PIN                GPIO_PIN_10
#define FDD_TRIG_PORT               GPIOA
#define FDD_TRIG_INT_EXTI_IRQn      EXTI15_10_IRQn

/* LEDs */
#define LED_RX_PIN                  GPIO_PIN_1
#define LED_RX_PORT                 GPIOB
#define LED_CONNECTED_PIN           GPIO_PIN_12
#define LED_CONNECTED_PORT          GPIOA

/* Analog Input for Random Number Generation */
#define RANDOM_ADC_PIN              GPIO_PIN_0
#define RANDOM_ADC_PORT             GPIOA
#define RANDOM_ADC_CHANNEL          ADC_CHANNEL_5

/* UART for HMI/Debug */
#define HMI_UART                    USART2
#define HMI_UART_TX_PIN             GPIO_PIN_2
#define HMI_UART_TX_PORT            GPIOA
#define HMI_UART_RX_PIN             GPIO_PIN_15
#define HMI_UART_RX_PORT            GPIOA
#define HMI_UART_BAUDRATE           921600

/* TIM2 for microsecond timing (TDMA) */
#define TDMA_TIMER                  TIM2
#define TDMA_TIMER_CLK_ENABLE()     __HAL_RCC_TIM2_CLK_ENABLE()

/* TIM3 for runtime statistics */
#define RTOS_STATS_TIMER            TIM3
#define RTOS_STATS_TIMER_CLK_ENABLE() __HAL_RCC_TIM3_CLK_ENABLE()

/* Exported constants --------------------------------------------------------*/

/* Queue sizes */
#define QUEUE_RADIO_TO_ETH_SIZE     6   /* Radio RX to Ethernet TX */
#define QUEUE_ETH_TO_RADIO_SIZE     6   /* Ethernet RX to Radio TX */
#define QUEUE_RADIO_ISR_SIZE        4   /* ISR to Radio task */
#define QUEUE_W5500_ISR_SIZE        4   /* W5500 interrupt events */

/* Maximum packet sizes */
#define MAX_RADIO_PACKET_SIZE       400
#define MAX_ETH_PACKET_SIZE         1518
#define RADIO_RX_FIFO_SIZE          0x2000  /* 8KB */
#define RADIO_TX_BUFFER_SIZE        128

/* External SRAM */
#define RADIO_ADDR_TABLE_SIZE       4

/* Exported macro ------------------------------------------------------------*/
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Critical section helpers */
#define ENTER_CRITICAL()  taskENTER_CRITICAL()
#define EXIT_CRITICAL()   taskEXIT_CRITICAL()

/* Exported variables --------------------------------------------------------*/
extern EventGroupHandle_t xSystemEvents;
extern SemaphoreHandle_t xSPI1_Mutex;  /* SI4463 */
extern SemaphoreHandle_t xSPI2_Mutex;  /* W5500 + ext SRAM */
extern SemaphoreHandle_t xPrintf_Mutex;

/* Global timer handles */
extern TIM_HandleTypeDef htim2;  /* TDMA microsecond timer */
extern TIM_HandleTypeDef htim3;  /* Runtime stats timer */

/* SPI handles */
extern SPI_HandleTypeDef hspi1;  /* SI4463 */
extern SPI_HandleTypeDef hspi2;  /* W5500 + SRAM */

/* UART handle */
extern UART_HandleTypeDef huart2;

/* ADC handle */
extern ADC_HandleTypeDef hadc1;

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* These are implemented as static in main.c */
/* void SystemClock_Config(void); */
/* void MX_GPIO_Init(void); */
/* void MX_SPI1_Init(void); */
/* void MX_SPI2_Init(void); */
/* void MX_USART2_UART_Init(void); */
/* void MX_TIM2_Init(void); */

/* Runtime stats */
void vConfigureTimerForRunTimeStats(void);
uint32_t ulGetRunTimeCounterValue(void);

/* Utility functions */
uint32_t HAL_GetUsTick(void);  /* Microsecond tick for TDMA timing */
void delay_us(uint32_t us);

/* Thread-safe printf */
int safe_printf(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
