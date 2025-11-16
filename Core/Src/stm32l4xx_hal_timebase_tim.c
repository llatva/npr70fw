/**
  ******************************************************************************
  * @file    stm32l4xx_hal_timebase_tim.c
  * @brief   HAL time base based on the hardware TIM6 instead of SysTick
  *          This is required when using FreeRTOS, which takes over SysTick.
  ******************************************************************************
  * @attention
  *
  * When FreeRTOS is used, SysTick is reserved for the RTOS scheduler.
  * The HAL time base must use a different timer. This file provides
  * HAL_InitTick() using TIM6.
  *
  ******************************************************************************
  */

#include "stm32l4xx_hal.h"

/* TIM6 handle for HAL time base */
TIM_HandleTypeDef htim6;

/**
  * @brief  This function configures TIM6 as a time base source.
  *         The time source is configured to have 1ms time base with a dedicated
  *         Tick interrupt priority.
  * @note   This function is called automatically at the beginning of program after
  *         reset by HAL_Init() or at any time when clock is configured, by HAL_RCC_ClockConfig().
  * @param  TickPriority: Tick interrupt priority.
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  RCC_ClkInitTypeDef    clkconfig;
  uint32_t              uwTimclock = 0;
  uint32_t              uwPrescalerValue = 0;
  uint32_t              pFLatency;
  HAL_StatusTypeDef     status;

  /* Enable TIM6 clock */
  __HAL_RCC_TIM6_CLK_ENABLE();

  /* Get clock configuration */
  HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);

  /* Compute TIM6 clock: APB1 timer clock */
  uwTimclock = HAL_RCC_GetPCLK1Freq();

  /* Compute the prescaler value to have TIM6 counter clock equal to 1MHz */
  uwPrescalerValue = (uint32_t)((uwTimclock / 1000000U) - 1U);

  /* Initialize TIM6 */
  htim6.Instance = TIM6;

  /* Initialize TIMx peripheral as follows:
       + Period = [(TIM6CLK/1000000) * 1000] - 1 = 999 (to get 1ms tick)
       + Prescaler = (TIM6CLK/1000000) - 1 (to get 1MHz counter clock)
       + ClockDivision = 0
       + Counter direction = Up
  */
  htim6.Init.Period = 1000U - 1U;  /* 1ms period (1000 * 1us = 1ms) */
  htim6.Init.Prescaler = uwPrescalerValue;
  htim6.Init.ClockDivision = 0;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  status = HAL_TIM_Base_Init(&htim6);
  if (status == HAL_OK)
  {
    /* Start the TIM6 Base generation in interrupt mode */
    status = HAL_TIM_Base_Start_IT(&htim6);
    if (status == HAL_OK)
    {
      /* Enable the TIM6 global Interrupt */
      HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

      /* Configure the TIM6 IRQ priority */
      if (TickPriority < (1UL << __NVIC_PRIO_BITS))
      {
        /* Configure the TIM6 priority */
        HAL_NVIC_SetPriority(TIM6_DAC_IRQn, TickPriority, 0U);
        uwTickPrio = TickPriority;
      }
      else
      {
        status = HAL_ERROR;
      }
    }
  }

  /* Return function status */
  return status;
}

/**
  * @brief  Suspend Tick increment.
  * @note   Disable the tick increment by disabling TIM6 update interrupt.
  * @param  None
  * @retval None
  */
void HAL_SuspendTick(void)
{
  /* Disable TIM6 update interrupt */
  __HAL_TIM_DISABLE_IT(&htim6, TIM_IT_UPDATE);
}

/**
  * @brief  Resume Tick increment.
  * @note   Enable the tick increment by enabling TIM6 update interrupt.
  * @param  None
  * @retval None
  */
void HAL_ResumeTick(void)
{
  /* Enable TIM6 update interrupt */
  __HAL_TIM_ENABLE_IT(&htim6, TIM_IT_UPDATE);
}

/**
  * @brief  TIM6 interrupt handler for HAL time base.
  * @note   This function handles TIM6 global interrupt request.
  * @param  None
  * @retval None
  */
void TIM6_DAC_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim6);
}

/**
  * @brief  Period elapsed callback in non-blocking mode
  * @note   This function is called when TIM6 or TIM2 interrupt took place, inside
  *         HAL_TIM_IRQHandler(). 
  *         - TIM6: Calls HAL_IncTick() to increment HAL time base (replaces SysTick)
  *         - TIM2: Increments microsecond overflow counter for TDMA timing
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    /* HAL time base tick (1ms) */
    HAL_IncTick();
  }
  else if (htim->Instance == TIM2)
  {
    /* TDMA microsecond timer overflow counter */
    extern volatile uint32_t g_microsecond_timer_overflow;
    g_microsecond_timer_overflow++;
  }
}
