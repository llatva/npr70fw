/**
  ******************************************************************************
  * @file    stm32l4xx_hal_msp.c
  * @brief   HAL MSP (MCU Support Package) module for peripheral initialization
  ******************************************************************************
  * This file provides the low-level hardware initialization functions that are
  * called by the HAL peripheral drivers (SPI, UART, TIM, etc.)
  ******************************************************************************
  */

#include "main.h"

/**
  * @brief UART MSP Initialization
  * @param huart UART handle pointer
  * @retval None
  */
void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  
  if(huart->Instance == USART2)
  {
    /* Peripheral clock enable */
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /**
     * USART2 GPIO Configuration
     * PA2  ------> USART2_TX
     * PA15 ------> USART2_RX (NOT PA3 - that's used for SI4463 interrupt!)
     */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA15 for RX */
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF3_USART2;  /* PA15 uses AF3 for USART2 */
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  }
}

/**
  * @brief UART MSP De-Initialization
  * @param huart UART handle pointer
  * @retval None
  */
void HAL_UART_MspDeInit(UART_HandleTypeDef* huart)
{
  if(huart->Instance == USART2)
  {
    /* Peripheral clock disable */
    __HAL_RCC_USART2_CLK_DISABLE();

    /**
     * USART2 GPIO Configuration
     * PA2  ------> USART2_TX
     * PA15 ------> USART2_RX
     */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_15);
  }
}

/**
  * @brief SPI MSP Initialization
  * @param hspi SPI handle pointer
  * @retval None
  */
void HAL_SPI_MspInit(SPI_HandleTypeDef* hspi)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  
  if(hspi->Instance == SPI1)
  {
    /* SPI1 clock enable */
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /**
     * SPI1 GPIO Configuration
     * PA5  ------> SPI1_SCK
     * PA6  ------> SPI1_MISO
     * PA7  ------> SPI1_MOSI
     */
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  }
  else if(hspi->Instance == SPI3)
  {
    /* SPI3 clock enable */
    __HAL_RCC_SPI3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /**
     * SPI3 GPIO Configuration (Note: L432KC uses alternate pins)
     * PB3  ------> SPI3_SCK
     * PB4  ------> SPI3_MISO
     * PB5  ------> SPI3_MOSI
     */
    GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  }
}

/**
  * @brief SPI MSP De-Initialization
  * @param hspi SPI handle pointer
  * @retval None
  */
void HAL_SPI_MspDeInit(SPI_HandleTypeDef* hspi)
{
  if(hspi->Instance == SPI1)
  {
    /* Peripheral clock disable */
    __HAL_RCC_SPI1_CLK_DISABLE();

    /**
     * SPI1 GPIO Configuration
     * PA5  ------> SPI1_SCK
     * PA6  ------> SPI1_MISO
     * PA7  ------> SPI1_MOSI
     */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
  }
  else if(hspi->Instance == SPI3)
  {
    /* Peripheral clock disable */
    __HAL_RCC_SPI3_CLK_DISABLE();

    /**
     * SPI3 GPIO Configuration
     * PB3  ------> SPI3_SCK
     * PB4  ------> SPI3_MISO
     * PB5  ------> SPI3_MOSI
     */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);
  }
}

/**
  * @brief TIM Base MSP Initialization
  * @param htim_base TIM Base handle pointer
  * @retval None
  */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* htim_base)
{
  if(htim_base->Instance == TIM2)
  {
    /* TIM2 clock enable */
    __HAL_RCC_TIM2_CLK_ENABLE();
    
    /* TIM2 interrupt init */
    HAL_NVIC_SetPriority(TIM2_IRQn, 15, 0);  /* Lowest priority */
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
  }
}

/**
  * @brief TIM Base MSP De-Initialization
  * @param htim_base TIM Base handle pointer
  * @retval None
  */
void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* htim_base)
{
  if(htim_base->Instance == TIM2)
  {
    /* Peripheral clock disable */
    __HAL_RCC_TIM2_CLK_DISABLE();

    /* TIM2 interrupt DeInit */
    HAL_NVIC_DisableIRQ(TIM2_IRQn);
  }
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
