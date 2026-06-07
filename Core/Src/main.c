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
#include "task_radio_combined.h"
#include "task_tdma.h"
#include "task_signaling.h"
#include "task_ethernet.h"
#include "task_dhcp_arp.h"
#include "task_telnet.h"
#include "task_serial_cli.h"
#include "task_monitor.h"
#include "w5500_driver.h"
#include "si4463_driver.h"
#include "ext_sram_driver.h"
#include "config_flash.h"
#include "watchdog.h"
#include <stdio.h>

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* HAL Peripheral handles */
SPI_HandleTypeDef hspi1;  /* SI4463 Radio */
SPI_HandleTypeDef hspi3;  /* W5500 Ethernet + External SRAM (SPI3 on L432KC) */
TIM_HandleTypeDef htim2;  /* TDMA timing (1 MHz) */
UART_HandleTypeDef huart2; /* Debug UART */

/* Driver handles (global so they can be accessed from other modules) */
static SI4463_Context_t hsi4463;
static W5500_Context_t hw5500;
ExtSRAM_Context_t hsram;  /* Non-static so it can be accessed from app_common.c */

/* FreeRTOS handles - will be initialized in main() */
// Task handles
TaskHandle_t xRadioISRHandlerTask = NULL; /* legacy handle kept for Watchdog naming */
TaskHandle_t xRadioProcessingTask = NULL; /* legacy handle kept for Watchdog naming */
TaskHandle_t xTDMATask = NULL;
TaskHandle_t xSignalingTask = NULL;
TaskHandle_t xEthernetRxTask = NULL;
TaskHandle_t xEthernetTxTask = NULL;
TaskHandle_t xDHCPARPTask = NULL;
TaskHandle_t xSNMPTask = NULL;
TaskHandle_t xTelnetTask = NULL;
TaskHandle_t xSerialCLITask = NULL;
TaskHandle_t xWatchdogTask = NULL;

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
extern void vRadioTask(void *pvParameters);
extern void vTDMATask(void *pvParameters);
extern void vSignalingTask(void *pvParameters);
extern void vEthernetRxTask(void *pvParameters);
extern void vEthernetTxTask(void *pvParameters);
extern void vDHCPARPTask(void *pvParameters);
extern void vSNMPTask(void *pvParameters);
extern void vTelnetTask(void *pvParameters);

/* Watchdog task - local implementation */
static void vWatchdogTask(void *pvParameters);

/* Private user code ---------------------------------------------------------*/

/**
  * @brief Watchdog monitoring task
  * @param pvParameters Not used
  */
static void vWatchdogTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    /* Register all tasks for monitoring */
    Watchdog_RegisterTask(xRadioISRHandlerTask, "RadioISR", 2000);
    Watchdog_RegisterTask(xRadioProcessingTask, "RadioProc", 2000);
    Watchdog_RegisterTask(xTDMATask, "TDMA", 2000);
    Watchdog_RegisterTask(xSignalingTask, "Signaling", 5000);
    Watchdog_RegisterTask(xEthernetRxTask, "EthRx", 2000);
    Watchdog_RegisterTask(xEthernetTxTask, "EthTx", 2000);
    
    for (;;) {
        /* Refresh hardware watchdog */
        Watchdog_Refresh();
        
        /* Check task watchdogs (optional - could log or take action) */
        Watchdog_CheckTasks();
        
        /* Run every 1 second */
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}

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

  /* Print boot banner - matching original NPR-70 style */
  printf("\r\n\r\nNPR-70, FreeRTOS FW v1.0\r\n");
  printf("Build: %s %s\r\n", __DATE__, __TIME__);
  printf("Serial: 921600 baud, 8N1\r\n");
  printf("=================================\r\n");
  printf("Boot: HAL Init OK\r\n");

  /* Start TIM2 for microsecond timing */
  HAL_TIM_Base_Start_IT(&htim2);
  printf("Boot: TIM2 started\r\n");

  /* Initialize watchdog (hardware only, task monitoring starts after scheduler) */
  Watchdog_Init();
  printf("Boot: Watchdog initialized\r\n");

  /* Initialize is_SRAM_ext to default before use */
  is_SRAM_ext = 0;  /* Default: no SRAM detected yet */

  /* Initialize application globals (before FreeRTOS) */
  InitializeGlobalVariables();
  printf("Boot: Global variables initialized\r\n");

  /* Create FreeRTOS synchronization primitives FIRST */
  /* Create Mutexes (needed for driver init) */
  xSPI1Mutex = xSemaphoreCreateMutex();
  if (xSPI1Mutex == NULL) {
    printf("FATAL: Failed to create SPI1 mutex!\r\n");
    Error_Handler();
  }
  xSPI3Mutex = xSemaphoreCreateMutex();
  if (xSPI3Mutex == NULL) {
    printf("FATAL: Failed to create SPI3 mutex!\r\n");
    Error_Handler();
  }
  xConfigMutex = xSemaphoreCreateMutex();
  if (xConfigMutex == NULL) {
    printf("FATAL: Failed to create Config mutex!\r\n");
    Error_Handler();
  }
  printf("Boot: Mutexes created\r\n");
  
  /* Initialize and TEST external SRAM early - NOW MANDATORY */
  /* External SRAM is REQUIRED for this firmware to operate */
  printf("Boot: Checking for external SRAM (REQUIRED)...\r\n");
  hsram.hspi = &hspi3;
  hsram.cs_port = GPIOB;
  hsram.cs_pin = GPIO_PIN_0;
  hsram.spi_mutex = xSPI3Mutex;
  
  HAL_StatusTypeDef sram_init_status = ExtSRAM_Init(&hsram);
  HAL_StatusTypeDef sram_test_status = HAL_ERROR;
  
  if (sram_init_status == HAL_OK) {
    /* SRAM initialized, now test read/write functionality */
    printf("Boot: External SRAM init OK, testing read/write...\r\n");
    sram_test_status = ExtSRAM_Test(&hsram);
  }
  
  if (sram_init_status != HAL_OK || sram_test_status != HAL_OK) {
    /* FATAL: External SRAM is REQUIRED but not working */
    printf("\r\n");
    printf("========================================\r\n");
    printf("FATAL ERROR: External SRAM NOT DETECTED\r\n");
    printf("========================================\r\n");
    printf("\r\n");
    printf("This firmware REQUIRES external SPI SRAM (23LC1024 or compatible)\r\n");
    printf("to operate. The external SRAM is used for:\r\n");
    printf("  - RX FIFO buffer (2KB)\r\n");
    printf("  - Packet buffers\r\n");
    printf("  - Queue storage\r\n");
    printf("\r\n");
    printf("Hardware Configuration:\r\n");
    printf("  - SRAM Chip: 23LC1024 (128KB SPI SRAM)\r\n");
    printf("  - SPI Bus: SPI3\r\n");
    printf("  - Chip Select: PB0\r\n");
    printf("\r\n");
    printf("Possible causes:\r\n");
    printf("  1. External SRAM chip not installed\r\n");
    printf("  2. Wiring/connection issue\r\n");
    printf("  3. SPI3 configuration problem\r\n");
    if (sram_init_status != HAL_OK) {
      printf("  4. SRAM initialization failed\r\n");
    } else {
      printf("  4. SRAM read/write test failed\r\n");
    }
    printf("\r\n");
    printf("System halted. Please install external SRAM and reboot.\r\n");
    printf("========================================\r\n");
    
    /* Halt the system - cannot continue without SRAM */
    while (1) {
      /* Blink LED to indicate error state */
      HAL_Delay(200);
    }
  }
  
  /* External SRAM is working! */
  is_SRAM_ext = 1;  /* Mark as available for use */
  printf("Boot: External SRAM detected and tested OK - using larger buffers\r\n");
  printf("Boot: SRAM Size: 128KB (23LC1024)\r\n");
  
  /* Create Queues with EXTERNAL sizes (SRAM is mandatory) */
  /* Always use larger queue sizes since we have external SRAM */
  uint8_t radio_tx_queue_size = RADIO_TX_QUEUE_SIZE_EXTERNAL;
  uint8_t eth_rx_queue_size = ETHERNET_RX_QUEUE_SIZE_EXTERNAL;
  uint8_t eth_tx_queue_size = ETHERNET_TX_QUEUE_SIZE_EXTERNAL;
  
  printf("Boot: Creating queues (RadioTx=%d, EthRx=%d, EthTx=%d)...\r\n", 
         radio_tx_queue_size, eth_rx_queue_size, eth_tx_queue_size);
  
  xRadioISRQueue = xQueueCreate(RADIO_ISR_QUEUE_SIZE, sizeof(RadioISREvent_t));
  if (xRadioISRQueue == NULL) {
    printf("FATAL: Failed to create RadioISR queue!\r\n");
    Error_Handler();
  }
  xRadioTxQueue = xQueueCreate(radio_tx_queue_size, sizeof(RadioRxPacket_t));
  if (xRadioTxQueue == NULL) {
    printf("FATAL: Failed to create RadioTx queue!\r\n");
    Error_Handler();
  }
  xEthernetRxQueue = xQueueCreate(eth_rx_queue_size, sizeof(EthernetPacket_t));
  if (xEthernetRxQueue == NULL) {
    printf("FATAL: Failed to create EthernetRx queue!\r\n");
    Error_Handler();
  }
  xEthernetTxQueue = xQueueCreate(eth_tx_queue_size, sizeof(EthernetPacket_t));
  if (xEthernetTxQueue == NULL) {
    printf("FATAL: Failed to create EthernetTx queue!\r\n");
    Error_Handler();
  }
  printf("Boot: Queues created\r\n");
  
  /* Create Event Groups */
  xSystemEvents = xEventGroupCreate();
  if (xSystemEvents == NULL) {
    printf("FATAL: Failed to create event group!\r\n");
    Error_Handler();
  }
  printf("Boot: Event groups created\r\n");

  /* Initialize and load configuration from flash (before scheduler starts) */
  Config_Flash_Init();
  printf("Boot: Config flash initialized\r\n");
  if (Config_Flash_Load() == HAL_OK) {
    /* Configuration loaded successfully from flash */
    printf("Boot: Config loaded from flash\r\n");
  } else {
    /* Using factory defaults (first boot or corrupted config) */
    printf("Boot: Using factory defaults\r\n");
  }

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
  
  /* External SRAM already initialized earlier (before queue creation) */
  /* Configuration structure was set up then */

  /* Initialize hardware drivers (now mutexes exist) */
  /* Note: Hardware may not be present - continue boot even if init fails */
  printf("Boot: Initializing W5500...\r\n");
  HAL_StatusTypeDef w5500_status = W5500_Init(&hw5500);
  if (w5500_status != HAL_OK) {
    printf("WARNING: W5500 init failed (not present?)\r\n");
    /* Don't call Error_Handler - allow boot to continue for debugging */
  } else {
    printf("Boot: W5500 OK\r\n");
    
    /* Skip socket configuration - will be done by tasks after scheduler starts
     * (socket config uses mutex which requires scheduler running)
     */
    #if 0
    /* Configure W5500 application sockets (DHCP, SNMP, Telnet) */
    if (W5500_ConfigureAppSockets(&hw5500) != HAL_OK) {
      printf("WARNING: W5500 socket config failed!\r\n");
    } else {
      printf("Boot: W5500 sockets configured\r\n");
    }
    #endif
  }
  
  printf("Boot: Initializing SI4463...\r\n");
  HAL_StatusTypeDef si4463_status = SI4463_Init(&hsi4463);
  if (si4463_status != HAL_OK) {
    printf("WARNING: SI4463 init failed (not present?)\r\n");
    /* Don't call Error_Handler - allow boot to continue for debugging */
  } else {
    printf("Boot: SI4463 OK\r\n");
  }
  
  /* External SRAM already initialized earlier */
  /* This was moved before queue creation to determine buffer sizes */
  
  /* Initialize task-specific modules */
  printf("Boot: Initializing task modules...\r\n");
  
  printf("  - RadioISRTask_Init...\r\n");
  RadioTask_Init(&hsi4463, &hw5500);
  
  printf("  - TDMATask_Init...\r\n");
  TDMATask_Init(&hsi4463);
  
  printf("  - SignalingTask_Init...\r\n");
  SignalingTask_Init(&hsi4463);
  
  printf("  - EthernetTask_Init...\r\n");
  EthernetTask_Init(&hw5500);
  printf("  - DHCPARPTask_Init...\r\n");
  DHCPARPTask_Init(&hw5500);
  
  printf("  - TelnetTask_Init...\r\n");
  TelnetTask_Init(&hw5500);
  
  printf("  - SerialCLI_Init...\r\n");
  SerialCLI_Init(&huart2);
  
  printf("  - MonitorTask_Init...\r\n");
  MonitorTask_Init(&hsi4463);
  
  printf("Boot: Task modules initialized\r\n");

  /* Create FreeRTOS tasks */
  printf("Boot: Creating FreeRTOS tasks...\r\n");
  
  /* Radio tasks - highest priority for timing-critical TDMA */
  /* Stack reduced conservatively by 10-15% based on typical usage patterns */
  if (xTaskCreate(vRadioTask, "Radio", 220, NULL, PRIORITY_RADIO_ISR_HANDLER, &xRadioISRHandlerTask) != pdPASS) {
    printf("FATAL: Failed to create Radio task!\r\n");
    Error_Handler();
  }
  if (xTaskCreate(vTDMATask, "TDMA", 144, NULL, PRIORITY_TDMA, &xTDMATask) != pdPASS) {
    printf("FATAL: Failed to create TDMA task!\r\n");
    Error_Handler();
  }
  if (xTaskCreate(vSignalingTask, "Signaling", 112, NULL, PRIORITY_SIGNALING, &xSignalingTask) != pdPASS) {
    printf("FATAL: Failed to create Signaling task!\r\n");
    Error_Handler();
  }
  
  /* Combined Ethernet task (RX+TX) */
  if (xTaskCreate(vEthernetTask, "Ethernet", 180, NULL, PRIORITY_ETH_RX, NULL) != pdPASS) {
    printf("FATAL: Failed to create Ethernet task!\r\n");
    Error_Handler();
  }
  /* Combined NetworkMgmt task (DHCP/ARP + SNMP) */
  /* DHCP/ARP task */
  if (xTaskCreate(vDHCPARPTask, "DHCP/ARP", 144, NULL, PRIORITY_DHCP_ARP, NULL) != pdPASS) {
    printf("FATAL: Failed to create DHCP/ARP task!\\r\\n");
    Error_Handler();
  }
  if (xTaskCreate(vTelnetTask, "Telnet", 144, NULL, PRIORITY_TELNET, &xTelnetTask) != pdPASS) {
    printf("FATAL: Failed to create Telnet task!\r\n");
    Error_Handler();
  }
  
  /* Serial CLI task - interactive USB console */
  if (xTaskCreate(vSerialCLITask, "SerialCLI", SERIAL_CLI_TASK_STACK_SIZE, NULL, SERIAL_CLI_TASK_PRIORITY, &xSerialCLITask) != pdPASS) {
    printf("FATAL: Failed to create SerialCLI task!\r\n");
    Error_Handler();
  }
  
  /* Watchdog task - lowest priority, runs periodically */
  if (xTaskCreate(vWatchdogTask, "Watchdog", 112, NULL, tskIDLE_PRIORITY + 1, &xWatchdogTask) != pdPASS) {
    printf("FATAL: Failed to create Watchdog task!\r\n");
    Error_Handler();
  }
  
  /* Monitor task - lowest priority, periodic health checks */
  /* Note: MonitorTask_Init already created the task, no xTaskCreate needed here */
  
  printf("Boot: All tasks created successfully\r\n");

  /* Start scheduler */
  printf("Boot: Starting FreeRTOS scheduler...\r\n\r\n");
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
  /* AutoReloadPreload not available in mbed HAL version - skip it */
  /* htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; */
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
  * @brief USART2 Initialization Function (Serial CLI - 921600 baud like original mbed code)
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 921600;  /* Same as original NPR-70 mbed firmware */
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
     free memory available in the FreeRTOS heap. Print diagnostics and halt
     so we can inspect the serial output. */
  size_t free_heap = xPortGetFreeHeapSize();
  printf("vApplicationMallocFailedHook: pvPortMalloc failed, free heap=%u\r\n", (unsigned int)free_heap);
  /* Wait in a loop to allow serial output to be observed */
  for (;;) {
    __BKPT(0);
  }
}

/**
  * @brief  FreeRTOS application stack overflow hook
  */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void) xTask;
  /* Report stack overflow info then halt */
  printf("vApplicationStackOverflowHook: Task '%s' overflowed stack\r\n", pcTaskName ? pcTaskName : "(unknown)");
  for (;;) {
    __BKPT(0);
  }
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
  
  /* Ensure GPIO clocks are enabled for LED (safe to call multiple times) */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  
  /* Configure LED pin if not already done */
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = LED_RX_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_RX_PORT, &GPIO_InitStruct);
  
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
