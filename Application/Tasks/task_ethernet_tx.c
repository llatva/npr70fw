/**
  ******************************************************************************
  * @file    task_ethernet_tx.c
  * @brief   Ethernet transmit task - send packets from queue to W5500
  ******************************************************************************
  * @attention
  *
  * This task receives Ethernet packets from the TX queue and transmits them
  * via the W5500 Ethernet controller. Handles flow control and retry logic.
  * Runs at priority 3 (medium priority).
  *
  ******************************************************************************
  */

#include "task_ethernet_tx.h"
#include "app_common.h"
#include "w5500_driver.h"
#include <stdio.h>
#include <string.h>

/* Private defines -----------------------------------------------------------*/
#define TX_TIMEOUT_MS       100     /* Timeout for W5500 TX operation */
#define TX_RETRY_DELAY_MS   10      /* Delay between retries */
#define MAX_TX_RETRIES      3       /* Maximum transmission attempts */

/* Private variables ---------------------------------------------------------*/
static W5500_Context_t *hw5500 = NULL;

/* Statistics */
static uint32_t tx_packet_count = 0;
static uint32_t tx_error_count = 0;

/**
 * @brief Initialize Ethernet TX task
 * @param w5500_ctx Pointer to W5500 driver context
 */
void EthernetTxTask_Init(W5500_Context_t *w5500_ctx)
{
    hw5500 = w5500_ctx;
}

/**
 * @brief Ethernet TX task - transmit packets from queue
 * @param argument Not used
 */
void vEthernetTxTask(void *argument)
{
    EthernetPacket_t eth_packet;
    HAL_StatusTypeDef status;
    uint8_t retry_count;
    
    printf("EthernetTx task started\r\n");
    
    for (;;) {
        /* Wait for packet from queue (block indefinitely) */
        if (xQueueReceive(xEthernetTxQueue, &eth_packet, portMAX_DELAY) == pdTRUE) {
            
            /* Validate packet */
            if (eth_packet.length > 0 && eth_packet.length <= sizeof(eth_packet.data)) {
                
                retry_count = 0;
                status = HAL_ERROR;
                
                /* Retry transmission on failure */
                while (retry_count < MAX_TX_RETRIES && status != HAL_OK) {
                    
                    /* Check if socket has free space */
                    uint16_t free_size = W5500_GetTxFreeSize(hw5500, eth_packet.socket);
                    
                    if (free_size >= eth_packet.length) {
                        /* Send packet via W5500 */
                        status = W5500_SendData(hw5500, eth_packet.socket, 
                                               eth_packet.data, eth_packet.length);
                        
                        if (status == HAL_OK) {
                            tx_packet_count++;
                        } else {
                            /* Transmission failed - retry */
                            retry_count++;
                            if (retry_count < MAX_TX_RETRIES) {
                                vTaskDelay(pdMS_TO_TICKS(TX_RETRY_DELAY_MS));
                            }
                        }
                    } else {
                        /* Socket buffer full - wait and retry */
                        retry_count++;
                        if (retry_count < MAX_TX_RETRIES) {
                            vTaskDelay(pdMS_TO_TICKS(TX_RETRY_DELAY_MS));
                        }
                    }
                }
                
                /* Check if all retries failed */
                if (status != HAL_OK) {
                    tx_error_count++;
                    /* TODO: Log error or notify higher layer */
                }
                
            } else {
                /* Invalid packet length */
                tx_error_count++;
            }
        }
    }
}

/**
 * @brief Get Ethernet TX statistics
 * @param tx_count Pointer to store transmitted packet count
 * @param tx_errors Pointer to store error count
 */
void EthernetTxTask_GetStats(uint32_t *tx_count, uint32_t *tx_errors)
{
    if (tx_count) {
        *tx_count = tx_packet_count;
    }
    if (tx_errors) {
        *tx_errors = tx_error_count;
    }
}
