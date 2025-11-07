/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body for NPR-70 FreeRTOS port
  ******************************************************************************
  * @attention
  *
  * NPR-70 Modem Firmware - FreeRTOS Port
  * 
  * This is the main entry point for the NPR-70 modem firmware running on
  * FreeRTOS 11.1.0 LTS with STM32 HAL drivers.
  *
  * Architecture:
  * - 9 FreeRTOS tasks with priority-based scheduling
  * - Deferred interrupt processing for timing-critical TDMA
  * - Mutex-protected SPI buses (SPI1 for SI4463, SPI2 for W5500+SRAM)
  * - Queue-based inter-task communication
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

/* Private includes ----------------------------------------------------------*/
#include "app_common.h"
#include "task_radio_isr.h"
#include "task_radio_processing.h"
#include "task_tdma.h"
#include "task_signaling.h"
#include "task_ethernet_rx.h"
#include "task_ethernet_tx.h"
#include "task_dhcp_arp.h"
#include "task_snmp.h"
#include "task_telnet.h"
#include "w5500_driver.h"
#include "si4463_driver.h"
#include "ext_sram_driver.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* HAL Peripheral handles */
SPI_HandleTypeDef hspi1;  /* SI4463 Radio */
SPI_HandleTypeDef hspi3;  /* W5500 Ethernet + External SRAM (SPI3 on L432KC) */
TIM_HandleTypeDef htim2;  /* TDMA timing (1 MHz) */
UART_HandleTypeDef huart2; /* Debug UART */

/* FreeRTOS handles - will be initialized in main() */
// Task handles
TaskHandle_t xRadioISRHandlerTask = NULL;
TaskHandle_t xRadioProcessingTask = NULL;
TaskHandle_t xTDMATask = NULL;
TaskHandle_t xSignalingTask = NULL;
TaskHandle_t xEthernetRxTask = NULL;
TaskHandle_t xEthernetTxTask = NULL;
TaskHandle_t xDHCPARPTask = NULL;
TaskHandle_t xSNMPTask = NULL;
TaskHandle_t xTelnetTask = NULL;

// Queue handles
QueueHandle_t xRadioISRQueue = NULL;
QueueHandle_t xRadioTxQueue = NULL;
QueueHandle_t xEthernetRxQueue = NULL;
QueueHandle_t xEthernetTxQueue = NULL;

// Mutex handles
SemaphoreHandle_t xSPI1Mutex = NULL;
SemaphoreHandle_t xSPI3Mutex = NULL;
SemaphoreHandle_t xConfigMutex = NULL;

// Event group handles
EventGroupHandle_t xSystemEvents = NULL;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_SPI3_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART2_UART_Init(void);

/* Task function prototypes - Implemented in Application/Tasks/ */
extern void vRadioISRHandlerTask(void *pvParameters);
extern void vRadioProcessingTask(void *pvParameters);
extern void vTDMATask(void *pvParameters);
extern void vSignalingTask(void *pvParameters);
extern void vEthernetRxTask(void *pvParameters);
extern void vEthernetTxTask(void *pvParameters);
extern void vDHCPARPTask(void *pvParameters);
extern void vSNMPTask(void *pvParameters);
extern void vTelnetTask(void *pvParameters);

/* Private user code ---------------------------------------------------------*/

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_SPI3_Init();
  MX_TIM2_Init();
  MX_USART2_UART_Init();

  /* Start TIM2 for microsecond timing */
  HAL_TIM_Base_Start_IT(&htim2);

  /* Initialize application globals */
  InitializeGlobalVariables();

  /* Initialize driver handles */
  static SI4463_Context_t hsi4463;
  static W5500_Context_t hw5500;
  static ExtSRAM_Context_t hsram;
  
  /* SI4463 Radio configuration */
  hsi4463.hspi = &hspi1;
  hsi4463.cs_port = GPIOA;
  hsi4463.cs_pin = GPIO_PIN_4;
  hsi4463.sdn_port = GPIOA;
  hsi4463.sdn_pin = GPIO_PIN_1;
  hsi4463.int_port = GPIOA;
  hsi4463.int_pin = GPIO_PIN_3;
  hsi4463.spi_mutex = xSPI1Mutex;
  
  /* W5500 Ethernet configuration */
  hw5500.hspi = &hspi3;
  hw5500.cs_port = GPIOA;
  hw5500.cs_pin = GPIO_PIN_11;
  hw5500.int_port = GPIOA;
  hw5500.int_pin = GPIO_PIN_8;
  hw5500.spi_mutex = xSPI3Mutex;
  
  /* External SRAM configuration */
  hsram.hspi = &hspi3;
  hsram.cs_port = GPIOB;
  hsram.cs_pin = GPIO_PIN_0;
  hsram.spi_mutex = xSPI3Mutex;

  /* Create FreeRTOS synchronization primitives */
  
  /* Create Queues */
  xRadioISRQueue = xQueueCreate(RADIO_ISR_QUEUE_SIZE, sizeof(RadioISREvent_t));
  xRadioTxQueue = xQueueCreate(RADIO_TX_QUEUE_SIZE, sizeof(RadioRxPacket_t));
  xEthernetRxQueue = xQueueCreate(ETHERNET_RX_QUEUE_SIZE, sizeof(EthernetPacket_t));
  xEthernetTxQueue = xQueueCreate(ETHERNET_TX_QUEUE_SIZE, sizeof(EthernetPacket_t));
  
  /* Create Mutexes */
  xSPI1Mutex = xSemaphoreCreateMutex();
  xSPI3Mutex = xSemaphoreCreateMutex();
  xConfigMutex = xSemaphoreCreateMutex();
  
  /* Create Event Groups */
  xSystemEvents = xEventGroupCreate();

  /* Initialize hardware drivers */
  if (W5500_Init(&hw5500) != HAL_OK) {
    Error_Handler();
  }
  
  if (SI4463_Init(&hsi4463) != HAL_OK) {
    Error_Handler();
  }
  
  /* Initialize external SRAM if present */
  is_SRAM_ext = (ExtSRAM_Init(&hsram) == HAL_OK) ? 1 : 0;
  
  /* Initialize task-specific modules */
  RadioISRTask_Init(&hsi4463);
  RadioProcessingTask_Init(&hw5500);
  TDMATask_Init(&hsi4463);
  SignalingTask_Init();
  EthernetRxTask_Init(&hw5500);
  EthernetTxTask_Init(&hw5500);
  DHCPARPTask_Init(&hw5500);
  SNMPTask_Init(&hw5500);
  TelnetTask_Init(&hw5500);

  /* Create FreeRTOS tasks */
  
  /* Radio tasks - highest priority for timing-critical TDMA */
  xTaskCreate(vRadioISRHandlerTask, "RadioISR", 512, NULL, PRIORITY_RADIO_ISR_HANDLER, &xRadioISRHandlerTask);
  xTaskCreate(vRadioProcessingTask, "RadioProc", 512, NULL, PRIORITY_RADIO_PROCESS, &xRadioProcessingTask);
  xTaskCreate(vTDMATask, "TDMA", 512, NULL, PRIORITY_TDMA, &xTDMATask);
  xTaskCreate(vSignalingTask, "Signaling", 384, NULL, PRIORITY_SIGNALING, &xSignalingTask);
  
  /* Ethernet tasks - medium priority */
  xTaskCreate(vEthernetRxTask, "EthRx", 512, NULL, PRIORITY_ETH_RX, &xEthernetRxTask);
  xTaskCreate(vEthernetTxTask, "EthTx", 384, NULL, PRIORITY_ETH_TX, &xEthernetTxTask);
  
  /* Service tasks - lower priority (stubs need minimal stack) */
  xTaskCreate(vDHCPARPTask, "DHCP_ARP", 256, NULL, PRIORITY_DHCP_ARP, &xDHCPARPTask);
  xTaskCreate(vSNMPTask, "SNMP", 384, NULL, PRIORITY_SNMP, &xSNMPTask);
  xTaskCreate(vTelnetTask, "Telnet", 256, NULL, PRIORITY_TELNET, &xTelnetTask);

  /* Start scheduler */
  vTaskStartScheduler();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  while (1)
  {
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;  /* 4 MHz */
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;  /* 4MHz * 40 / 1 = 160 MHz VCO */
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;  /* 160 MHz / 2 = 80 MHz */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;  /* 80 MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;   /* 80 MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;   /* 80 MHz */

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function (SI4463 Radio)
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;  /* 80MHz/8 = 10MHz */
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI3 Initialization Function (W5500 + External SRAM)
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_MASTER;
  hspi3.Init.Direction = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;  /* 80MHz/4 = 20MHz */
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 7;
  hspi3.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi3.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function (TDMA Timing - 1 MHz)
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 79;  /* 80MHz / (79+1) = 1MHz = 1μs resolution */
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 0xFFFFFFFF;  /* 32-bit timer, max period */
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  
  /* Start the timer */
  HAL_TIM_Base_Start(&htim2);
}

/**
  * @brief USART2 Initialization Function (Debug UART)
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level - SI4463 pins */
  HAL_GPIO_WritePin(GPIOA, SI4463_CS_PIN|SI4463_SDN_PIN, GPIO_PIN_SET);
  
  /*Configure GPIO pin Output Level - W5500 and SRAM pins */
  HAL_GPIO_WritePin(GPIOB, W5500_CS_PIN|EXT_SRAM_CS_PIN, GPIO_PIN_SET);
  
  /*Configure GPIO pin Output Level - Status LEDs */
  HAL_GPIO_WritePin(GPIOB, LED_RX_PIN|LED_CONNECTED_PIN, GPIO_PIN_RESET);

  /*Configure GPIO pins : SI4463_CS_PIN SI4463_SDN_PIN */
  GPIO_InitStruct.Pin = SI4463_CS_PIN|SI4463_SDN_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : W5500_CS_PIN EXT_SRAM_CS_PIN LED_RX_PIN LED_CONNECTED_PIN */
  GPIO_InitStruct.Pin = W5500_CS_PIN|EXT_SRAM_CS_PIN|LED_RX_PIN|LED_CONNECTED_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : SI4463_INT_PIN */
  GPIO_InitStruct.Pin = SI4463_INT_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SI4463_INT_PORT, &GPIO_InitStruct);

  /*Configure GPIO pin : W5500_INT_PIN */
  GPIO_InitStruct.Pin = W5500_INT_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(W5500_INT_PORT, &GPIO_InitStruct);

  /* EXTI interrupt init - SI4463 */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);  /* High priority for radio */
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* EXTI interrupt init - W5500 */
  HAL_NVIC_SetPriority(EXTI1_IRQn, 6, 0);  /* Medium priority for ethernet */
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);
}

/* ============================================================================ */
/* HAL Callbacks                                                                */
/* ============================================================================ */

/**
  * @brief  TIM2 period elapsed callback - increments microsecond overflow counter
  * @param  htim TIM handle
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    extern volatile uint32_t g_microsecond_timer_overflow;
    g_microsecond_timer_overflow++;
  }
}

/* ============================================================================ */
/* Task Implementations - Placeholder stubs for now                             */
/* ============================================================================ */

/* Note: vRadioISRHandlerTask is implemented in Application/Tasks/task_radio_isr.c */
/* Note: vRadioProcessingTask is implemented in Application/Tasks/task_radio_processing.c */
/* Note: vTDMATask is implemented in Application/Tasks/task_tdma.c */
/* Note: vEthernetRxTask is implemented in Application/Tasks/task_ethernet_rx.c */
/* Note: vEthernetTxTask is implemented in Application/Tasks/task_ethernet_tx.c */
/* Note: vDHCPARPTask is implemented in Application/Tasks/task_dhcp_arp.c */
/* Note: vSNMPTask is implemented in Application/Tasks/task_snmp.c */
/* Note: vTelnetTask is implemented in Application/Tasks/task_telnet.c */

/* ============================================================================ */
/* FreeRTOS Callback Hooks                                                     */
/* ============================================================================ */

/**
  * @brief  FreeRTOS application malloc failed hook
  */
void vApplicationMallocFailedHook(void)
{
  /* Called if a call to pvPortMalloc() fails because there is insufficient
     free memory available in the FreeRTOS heap.  pvPortMalloc() is called
     internally by FreeRTOS API functions that create tasks, queues, etc. */
  Error_Handler();
}

/**
  * @brief  FreeRTOS application stack overflow hook
  */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void) pcTaskName;
  (void) xTask;

  /* Run time stack overflow checking is performed if
     configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
     function is called if a stack overflow is detected. */
  Error_Handler();
}

/**
  * @brief  Configure timer for runtime stats
  */
void vConfigureTimerForRunTimeStats(void)
{
  /* Use TIM2 counter for runtime stats - already running at 1MHz from MX_TIM2_Init() */
}

/**
  * @brief  Get runtime counter value
  */
uint32_t ulGetRunTimeCounterValue(void)
{
  /* Return current TIM2 counter value (1 MHz, 1μs resolution) */
  return __HAL_TIM_GET_COUNTER(&htim2);
}

/**
  * @brief  FreeRTOS application idle hook
  */
void vApplicationIdleHook(void)
{
  /* Called on each iteration of the idle task */
  /* Can be used for low priority background tasks or power saving */
}

/**
  * @brief  Get idle task memory (for static allocation if enabled)
  */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint16_t *pulIdleTaskStackSize)
{
  static StaticTask_t xIdleTaskTCB;
  static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

  *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
  *ppxIdleTaskStackBuffer = uxIdleTaskStack;
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/**
  * @brief  Get timer task memory (for static allocation if enabled)
  */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     uint16_t *pulTimerTaskStackSize)
{
  static StaticTask_t xTimerTaskTCB;
  static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

  *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
  *ppxTimerTaskStackBuffer = uxTimerTaskStack;
  *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/* ============================================================================ */
/* Error Handler                                                                */
/* ============================================================================ */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
    /* Toggle LED to indicate error */
    HAL_GPIO_TogglePin(LED_RX_PORT, LED_RX_PIN);
    for(volatile uint32_t i = 0; i < 1000000; i++);
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT NPR-70 FreeRTOS Port *****END OF FILE****/
