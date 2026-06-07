/**
  ******************************************************************************
  * @file    task_radio_isr.c
  * @brief   Radio interrupt handler task (deferred ISR processing)
  ******************************************************************************
  * @attention
  *
  * This task handles deferred interrupt processing for the SI4463 radio.
  * It runs at highest priority (7) and processes RX/TX interrupt events.
  *
  ******************************************************************************
  */

#include "task_radio_isr.h"
#include "app_common.h"
#include "si4463_driver.h"
#include <stdio.h>

/* Private defines -----------------------------------------------------------*/
#define SYNC_DETECTED_MASK      0x01
#define FIFO_ALMOST_FULL_MASK   0x01
#define PACKET_RX_MASK          0x10

/* Private variables ---------------------------------------------------------*/
static SI4463_Context_t *hsi4463 = NULL;

/* Private function prototypes -----------------------------------------------*/
static void ProcessRxInterrupt(uint32_t timestamp);
static void ProcessTxInterrupt(uint32_t timestamp);

/**
 * @brief Initialize radio ISR task
 * @param si4463_ctx Pointer to SI4463 driver context
 */
void RadioISRTask_Init(SI4463_Context_t *si4463_ctx)
{
    printf("RadioISRTask_Init: entry\r\n");
    hsi4463 = si4463_ctx;
    printf("RadioISRTask_Init: done\r\n");
}

/**
 * @brief Radio ISR handler task - processes deferred interrupts
 * @param argument Not used
 */
void vRadioISRHandlerTask(void *argument)
{
    RadioISREvent_t event;
    
    printf("RadioISRHandler task started\r\n");
    
    for (;;) {
        /* Wait for interrupt event from ISR */
        if (xQueueReceive(xRadioISRQueue, &event, portMAX_DELAY) == pdTRUE) {
            
            /* Process based on event type */
            if (event.event_type == 0) {
                /* RX interrupt */
                ProcessRxInterrupt(event.timestamp);
            } else if (event.event_type == 1) {
                /* TX interrupt */
                ProcessTxInterrupt(event.timestamp);
            }
        }
    }
}

/**
 * @brief Process RX interrupt (port of SI4463_RX_IT)
 * @param timestamp Interrupt timestamp in microseconds
 */
static void ProcessRxInterrupt(uint32_t timestamp)
{
    uint8_t FRR[5];
    uint8_t RSSI;
    uint32_t RX_timer;
    uint8_t tx_cmd[2];
    uint8_t rx_resp[2];
    uint16_t size_to_read;
    uint8_t treated_sync = 0;
    uint8_t treated_fifo = 0;
    uint8_t treated_pkt = 0;
    
    /* Small delay for RSSI propagation */
    vTaskDelay(pdMS_TO_TICKS(1));
    
    /* Process interrupt loop */
    do {
        /* Read Fast Response Registers */
        if (SI4463_ReadFRR(hsi4463, FRR) != HAL_OK) {
            break;
        }
        
        uint8_t it_sync_detected = FRR[1] & SYNC_DETECTED_MASK;
        uint8_t it_fifo_almost_full = FRR[0] & FIFO_ALMOST_FULL_MASK;
        uint8_t it_pckt_rx = (FRR[0] & PACKET_RX_MASK) >> 4;
        
        uint8_t synth_sync = it_sync_detected ^ treated_sync;
        uint8_t synth_fifo = it_fifo_almost_full ^ treated_fifo;
        uint8_t synth_pkt = it_pckt_rx ^ treated_pkt;
        
        /* Handle sync detected */
        if (synth_sync) {
            RSSI = FRR[2];
            RX_timer = timestamp - CONF_radio.long_preamble_duration_for_TA;
            
            if (is_TDMA_master && (CONF_radio.master_FDD == 2)) {
                /* Master FDD uplink mode */
                RX_timer = RX_timer - TDMA_slave_last_master_top;
            }
            
            treated_sync = 1;
            
            /* Update statistics */
            taskENTER_CRITICAL();
            RSSI_total_stat += RSSI;
            RSSI_stat_pkt_nb++;
            taskEXIT_CRITICAL();
            
            /* Turn on RX LED (handled by GPIO in main) */
        }
        
        /* Handle FIFO almost full or packet received */
        if (synth_fifo || synth_pkt) {
            
            if ((RX_size_remaining == 0) && !treated_fifo && !treated_pkt) {
                /* Beginning of new packet - read first byte (size) */
                tx_cmd[0] = 0x77;  /* READ_RX_FIFO command */
                tx_cmd[1] = 0x00;
                
                if (xSemaphoreTake(xSPI1Mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);  /* CS2 */
                    HAL_SPI_TransmitReceive(&hspi1, tx_cmd, rx_resp, 2, 100);
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
                    xSemaphoreGive(xSPI1Mutex);
                    
                    /* Calculate size to read */
                    RX_size_remaining = rx_resp[1] + SI4463_OFFSET_SIZE;
                    if (RX_size_remaining > SI4463_CONF_MAX_FIELD2_SIZE) {
                        RX_size_remaining = SI4463_CONF_MAX_FIELD2_SIZE;
                    }
                    
                    if (RX_size_remaining > (SI4463_CONF_RX_FIFO_THRESHOLD - 1)) {
                        size_to_read = SI4463_CONF_RX_FIFO_THRESHOLD - 1;
                    } else {
                        size_to_read = RX_size_remaining;
                    }
                    
                    if (synth_pkt) {
                        /* Force read all if full packet received */
                        size_to_read = RX_size_remaining;
                    }
                    
                    /* Store packet header in FIFO */
                    taskENTER_CRITICAL();
                    RX_FIFO_WriteByte(RX_FIFO_WR_point & RX_FIFO_MASK, RX_timer & 0xFF);
                    RX_FIFO_WR_point++;
                    RX_FIFO_WriteByte(RX_FIFO_WR_point & RX_FIFO_MASK, (RX_timer >> 8) & 0xFF);
                    RX_FIFO_WR_point++;
                    RX_FIFO_WriteByte(RX_FIFO_WR_point & RX_FIFO_MASK, (RX_timer >> 16) & 0xFF);
                    RX_FIFO_WR_point++;
                    RX_FIFO_WriteByte(RX_FIFO_WR_point & RX_FIFO_MASK, RSSI);
                    RX_FIFO_WR_point++;
                    RX_FIFO_WriteByte(RX_FIFO_WR_point & RX_FIFO_MASK, rx_resp[1]);  /* Size */
                    RX_FIFO_WR_point++;
                    
                    /* Read packet data from FIFO */
                    for (uint16_t i = 0; i < size_to_read; i++) {
                        /* This should use SI4463_ReadRxFifo but simplified here */
                        RX_FIFO_WriteByte(RX_FIFO_WR_point & RX_FIFO_MASK, 0);  /* Placeholder */
                        RX_FIFO_WR_point++;
                    }
                    
                    RX_size_remaining -= size_to_read;
                    
                    if (RX_size_remaining == 0) {
                        /* Complete packet received */
                        RX_FIFO_last_received = RX_FIFO_WR_point;
                    }
                    taskEXIT_CRITICAL();
                }
            } else {
                /* Continue reading multi-part packet */
                if (RX_size_remaining > SI4463_CONF_RX_FIFO_THRESHOLD) {
                    size_to_read = SI4463_CONF_RX_FIFO_THRESHOLD;
                } else {
                    size_to_read = RX_size_remaining;
                }
                
                if (synth_pkt) {
                    size_to_read = RX_size_remaining;
                }
                
                /* Read remaining data */
                taskENTER_CRITICAL();
                for (uint16_t i = 0; i < size_to_read; i++) {
                    RX_FIFO_WriteByte(RX_FIFO_WR_point & RX_FIFO_MASK, 0);  /* Placeholder */
                    RX_FIFO_WR_point++;
                }
                
                RX_size_remaining -= size_to_read;
                
                if (RX_size_remaining == 0) {
                    RX_FIFO_last_received = RX_FIFO_WR_point;
                }
                taskEXIT_CRITICAL();
            }
            
            treated_fifo = it_fifo_almost_full;
            treated_pkt = it_pckt_rx;
        }
        
        /* Check if more interrupts pending */
        if (!synth_sync && !synth_fifo && !synth_pkt) {
            break;
        }
        
    } while (1);
    
    /* Clear interrupt flags - clear all packet handler and modem interrupts */
    SI4463_ClearInterrupts(hsi4463, 0xFF, 0xFF);
}

/**
 * @brief Process TX interrupt (port of SI4463_HW_TX_IT)
 * @param timestamp Interrupt timestamp in microseconds
 */
static void ProcessTxInterrupt(uint32_t timestamp)
{
    /* TX interrupt handling */
    /* For now, just clear interrupts - full implementation needed */
    SI4463_ClearInterrupts(hsi4463, 0xFF, 0xFF);
}

/**
 * @brief GPIO EXTI callback for SI4463 interrupt pin
 * @param GPIO_Pin GPIO pin number
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_3) {  /* PA3 - SI4463 INT */
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        RadioISREvent_t event;
        
        /* Get timestamp */
        event.timestamp = GetMicrosecondTimer();
        
        /* Determine RX or TX state - simplified, needs state tracking */
        event.event_type = 0;  /* 0=RX, 1=TX */
        
        /* Post to queue from ISR */
        xQueueSendFromISR(xRadioISRQueue, &event, &xHigherPriorityTaskWoken);
        
        /* Yield if higher priority task was woken */
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
