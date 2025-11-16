/* FreeRTOS Configuration for NPR-70 Modem - STM32L432KC */
/* Optimized for real-time TDMA radio timing requirements */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * Application specific definitions for STM32L432KC
 *
 * Target: STM32L432KC (Cortex-M4F @ 80MHz)
 * RAM: 64KB, Flash: 256KB
 * External SRAM: Available via SPI
 *----------------------------------------------------------*/

/* FreeRTOS kernel version */
#define configFREERTOS_VERSION_MAJOR        11
#define configFREERTOS_VERSION_MINOR        1

/* Cortex-M specific definitions */
#ifdef __NVIC_PRIO_BITS
  #define configPRIO_BITS __NVIC_PRIO_BITS
#else
  #define configPRIO_BITS 4  /* STM32L4 has 4 bits for priority */
#endif

/* The lowest interrupt priority that can be used by FreeRTOS */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   15

/* The highest interrupt priority for system critical interrupts */
/* SI4463 radio interrupt needs very low latency for TDMA timing */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

/* Kernel interrupt priorities */
#define configKERNEL_INTERRUPT_PRIORITY \
  (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/*-----------------------------------------------------------
 * Core FreeRTOS functionality
 *----------------------------------------------------------*/
#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0  /* No low-power, timing is critical */
#define configCPU_CLOCK_HZ                      80000000  /* 80MHz */
#define configSYSTICK_CLOCK_HZ                  80000000
#define configTICK_RATE_HZ                      1000      /* 1ms tick for good timing resolution */
#define configMAX_PRIORITIES                    8         /* 0-7, allows fine-grained priority control */
#define configMINIMAL_STACK_SIZE                128       /* Words (512 bytes) */
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0         /* Must be 0 for Cortex-M */
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   3
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_ALTERNATIVE_API               0         /* Deprecated */
#define configQUEUE_REGISTRY_SIZE               10
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              1         /* Thread-safe newlib */
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5
#define configSTACK_DEPTH_TYPE                  uint16_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t
#define configHEAP_CLEAR_MEMORY_ON_FREE         1

/*-----------------------------------------------------------
 * Memory allocation
 *----------------------------------------------------------*/
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t)(12 * 1024))  /* 12KB heap - carefully tuned after moving buffers to SRAM2 */
#define configAPPLICATION_ALLOCATED_HEAP        0

/*-----------------------------------------------------------
 * Hook functions
 *----------------------------------------------------------*/
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2  /* Method 2: full stack checking */
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/*-----------------------------------------------------------
 * Run time and task stats gathering
 *----------------------------------------------------------*/
#define configGENERATE_RUN_TIME_STATS           1
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1

/* Runtime stats use TIM2 for high-resolution timing */
extern void vConfigureTimerForRunTimeStats(void);
extern uint32_t ulGetRunTimeCounterValue(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() vConfigureTimerForRunTimeStats()
#define portGET_RUN_TIME_COUNTER_VALUE()         ulGetRunTimeCounterValue()

/*-----------------------------------------------------------
 * Co-routine definitions (not used)
 *----------------------------------------------------------*/
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2

/*-----------------------------------------------------------
 * Software timer definitions
 *----------------------------------------------------------*/
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               6  /* High priority for TDMA timing */
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

/*-----------------------------------------------------------
 * Optional FreeRTOS+ functions
 *----------------------------------------------------------*/
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xEventGroupSetBitFromISR        1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xTaskResumeFromISR              1

/*-----------------------------------------------------------
 * Cortex-M specific definitions
 *----------------------------------------------------------*/
/* Map FreeRTOS handlers to the names used in startup file */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* Ensure Cortex-M port functions are available */
/* Enhanced configASSERT with printf for better debugging */
#include <stdio.h>
extern void Error_Handler(void);
#define configASSERT(x) if((x) == 0) { \
    printf("\r\n*** ASSERT FAILED: %s:%d ***\r\n", __FILE__, __LINE__); \
    Error_Handler(); \
}

/* Definitions for backward compatibility with older FreeRTOS versions */
#define configUSE_APPLICATION_TASK_TAG          0

/*-----------------------------------------------------------
 * NPR-70 Specific Definitions
 *----------------------------------------------------------*/
/* Task priorities (0 = lowest, 7 = highest) */
#define PRIORITY_IDLE                           0
#define PRIORITY_MONITOR                        1
#define PRIORITY_TELNET                         2
#define PRIORITY_SIGNALING                      2
#define PRIORITY_SNMP                           3
#define PRIORITY_DHCP_ARP                       3
#define PRIORITY_ETH_TX                         4
#define PRIORITY_ETH_RX                         5
#define PRIORITY_RADIO_PROCESS                  6
#define PRIORITY_TDMA                           6  /* Same as radio processing */
#define PRIORITY_RADIO_ISR_HANDLER              7  /* Highest - time critical TDMA */

/* Stack sizes (in words, 1 word = 4 bytes) */
#define STACK_SIZE_RADIO_ISR_HANDLER            512   /* 2KB - ISR deferred processing */
#define STACK_SIZE_RADIO_PROCESS                512   /* 2KB - Radio packet processing */
#define STACK_SIZE_ETH_RX                       384   /* 1.5KB */
#define STACK_SIZE_ETH_TX                       384   /* 1.5KB */
#define STACK_SIZE_DHCP_ARP                     256   /* 1KB */
#define STACK_SIZE_SNMP                         384   /* 1.5KB */
#define STACK_SIZE_TELNET                       512   /* 2KB - String processing */
#define STACK_SIZE_SIGNALING                    384   /* 1.5KB */
#define STACK_SIZE_MONITOR                      256   /* 1KB */

#endif /* FREERTOS_CONFIG_H */
