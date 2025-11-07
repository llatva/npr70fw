/**
  ******************************************************************************
  * @file    task_tdma.c
  * @brief   TDMA time division multiple access synchronization task
  ******************************************************************************
  * @attention
  *
  * This task manages TDMA frame timing and slot allocation.
  * In master mode: allocates slots to clients
  * In client mode: monitors master sync and handles timeouts
  * Runs at priority 6 (high priority for timing accuracy).
  *
  ******************************************************************************
  */

#include "task_tdma.h"
#include "app_common.h"
#include "si4463_driver.h"
#include <stdio.h>
#include <string.h>

/* Private defines -----------------------------------------------------------*/
#define TDMA_FRAME_DURATION_US      100000  /* 100ms frame duration (configurable) */
#define TDMA_TIMEOUT_MARGIN_US      10000   /* 10ms timeout margin */
#define TDMA_SLAVE_TIMEOUT_US       (TDMA_FRAME_DURATION_US + 8000)
#define TDMA_SLAVE_TIMEOUT_MAX_US   (TDMA_FRAME_DURATION_US + 10000)

/* TDMA table sizes */
#define MAX_TDMA_CLIENTS            RADIO_ADDR_TABLE_SIZE

/* Private variables ---------------------------------------------------------*/
static SI4463_Context_t *hsi4463 = NULL;

/* TDMA tables (master mode) */
static uint8_t tdma_table_uplink_st[MAX_TDMA_CLIENTS];      /* Client uplink status */
static uint16_t tdma_table_uplink_usage[MAX_TDMA_CLIENTS];  /* Client buffer usage */
static uint8_t tdma_table_is_fast[MAX_TDMA_CLIENTS];        /* Fast/slow slot flag */
static uint32_t tdma_table_rx_time[MAX_TDMA_CLIENTS];       /* Last RX timestamp */
static uint8_t tdma_table_up2date[MAX_TDMA_CLIENTS];        /* Recently updated flag */
static uint8_t tdma_table_slots[MAX_TDMA_CLIENTS];          /* Allocated slots */
static uint32_t tdma_table_offset[MAX_TDMA_CLIENTS];        /* Timing offsets */

/* TDMA frame counter */
static uint8_t tdma_frame_nb = 0;

/* Client mode variables */
static uint8_t slave_alloc_rx_age = 2;
static uint8_t my_multiframe_mask = 0x07;   /* Default: 8-frame multiframe */
static uint8_t my_multiframe_id = 0;

/* Configuration (should come from config flash) */
static uint32_t conf_tdma_frame_duration = TDMA_FRAME_DURATION_US;

/* Statistics */
static uint32_t tdma_sync_count = 0;
static uint32_t tdma_timeout_count = 0;

/**
 * @brief Initialize TDMA task
 * @param si4463_ctx Pointer to SI4463 driver context
 */
void TDMATask_Init(SI4463_Context_t *si4463_ctx)
{
    int i;
    
    hsi4463 = si4463_ctx;
    
    /* Initialize TDMA tables */
    for (i = 0; i < MAX_TDMA_CLIENTS; i++) {
        tdma_table_uplink_st[i] = 0;
        tdma_table_uplink_usage[i] = 32;
        tdma_table_is_fast[i] = 1;
        tdma_table_rx_time[i] = 0;
        tdma_table_up2date[i] = 0;
        tdma_table_slots[i] = 0;
        tdma_table_offset[i] = 0;
    }
    
    /* Load configuration from flash */
    /* TODO: Read from config flash */
    conf_tdma_frame_duration = TDMA_FRAME_DURATION_US;
}

/**
 * @brief TDMA task - timing synchronization and slot allocation
 * @param argument Not used
 */
void vTDMATask(void *argument)
{
    uint32_t current_time;
    uint32_t master_top_age;
    uint32_t last_check_time = 0;
    
    printf("TDMA task started\r\n");
    
    for (;;) {
        current_time = GetMicrosecondTimer();
        
        if (is_TDMA_master) {
            /* ========== MASTER MODE ========== */
            
            /* Master mode TDMA allocation logic */
            /* TODO: Implement slot allocation algorithm
             * - Track client buffer usage
             * - Allocate fast/slow slots
             * - Send TDMA allocation frames
             */
            
            /* For now, just increment frame counter periodically */
            if ((current_time - last_check_time) >= conf_tdma_frame_duration) {
                tdma_frame_nb = (tdma_frame_nb + 1) & 0x1F;  /* 5-bit frame counter */
                last_check_time = current_time;
                tdma_sync_count++;
            }
            
        } else {
            /* ========== CLIENT MODE ========== */
            
            /* Monitor master sync timeout */
            master_top_age = current_time - TDMA_slave_last_master_top;
            
            /* Check for sync timeout */
            if (master_top_age > TDMA_SLAVE_TIMEOUT_US && 
                master_top_age < TDMA_SLAVE_TIMEOUT_MAX_US) {
                
                /* Allow one more TX burst if:
                 * - Recently received allocation
                 * - No pending TX
                 * - Radio is enabled
                 */
                if (slave_alloc_rx_age < 2 && CONF_radio.state_ON_OFF) {
                    
                    /* Increment frame counter */
                    tdma_frame_nb = (tdma_frame_nb + 1) & 0x1F;
                    
                    /* Check if this is our allocated slot (multiframe) */
                    if ((tdma_frame_nb & my_multiframe_mask) == 
                        (my_multiframe_id & my_multiframe_mask)) {
                        
                        /* This is our slot - prepare TX */
                        /* TODO: Schedule radio TX via timer or direct call */
                        /* For now, just count */
                        
                        slave_alloc_rx_age++;
                    }
                    
                    tdma_timeout_count++;
                }
            }
            
            /* Age out allocation */
            if (master_top_age > (conf_tdma_frame_duration * 3)) {
                /* No sync for 3+ frames - lost connection */
                if (my_client_radio_connexion_state == 2) {
                    my_client_radio_connexion_state = 1;  /* Connected -> connecting */
                }
            }
        }
        
        /* Update TDMA table up2date flags (master mode) */
        if (is_TDMA_master && (current_time - last_check_time) > 1000) {
            for (int i = 0; i < MAX_TDMA_CLIENTS; i++) {
                if (CONF_radio.addr_table_status[i] && !tdma_table_up2date[i]) {
                    /* Client didn't report in this frame - reduce uplink status */
                    if (tdma_table_uplink_st[i] > 0) {
                        tdma_table_uplink_st[i]--;
                    }
                }
                tdma_table_up2date[i] = 0;  /* Clear for next frame */
            }
        }
        
        /* Sleep to avoid busy loop - wake every 1ms for timing accuracy */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief Update TDMA client information (called from radio processing)
 * @param client_id Client radio address
 * @param buffer_usage Client's reported buffer usage
 * @param timestamp Reception timestamp
 */
void TDMA_UpdateClientInfo(uint8_t client_id, uint8_t buffer_usage, uint32_t timestamp)
{
    if (client_id >= MAX_TDMA_CLIENTS) {
        return;
    }
    
    if (is_TDMA_master) {
        taskENTER_CRITICAL();
        tdma_table_uplink_st[client_id] = buffer_usage;
        tdma_table_uplink_usage[client_id] = buffer_usage;
        tdma_table_rx_time[client_id] = timestamp;
        tdma_table_up2date[client_id] = 1;
        taskEXIT_CRITICAL();
    }
}

/**
 * @brief Process TDMA allocation frame (client mode)
 * @param alloc_data Allocation frame data
 * @param size Frame size
 */
void TDMA_ProcessAllocation(const uint8_t *alloc_data, uint16_t size)
{
    /* TODO: Parse allocation frame
     * - Extract slot assignment
     * - Update my_multiframe_mask and my_multiframe_id
     * - Calculate timing offset
     * 
     * For now, just update age counter
     */
    
    if (!is_TDMA_master) {
        slave_alloc_rx_age = 0;  /* Reset age - we got fresh allocation */
        my_client_radio_connexion_state = 2;  /* Connected */
    }
}

/**
 * @brief Generate TDMA byte for transmission
 * @param is_sync Set to 1 for sync frame (first frame in sequence)
 * @return TDMA control byte with parity
 */
uint8_t TDMA_GenerateByte(uint8_t is_sync)
{
    uint8_t tdma_byte;
    uint32_t uplink_buffer_size;
    
    if (is_TDMA_master) {
        /* Master: downlink bit + frame counter */
        tdma_byte = 0x40;  /* Downlink bit set */
        tdma_byte |= (tdma_frame_nb & 0x1F);
        
        if (is_sync) {
            tdma_frame_nb = (tdma_frame_nb + 1) & 0x1F;
        }
        
    } else {
        /* Client: uplink bit + buffer usage */
        tdma_byte = 0x00;  /* Uplink (downlink bit clear) */
        
        /* Report our TX buffer usage */
        uplink_buffer_size = 0;  /* TODO: Get actual TX buffer size */
        if (uplink_buffer_size > 30) {
            uplink_buffer_size = 30;
        }
        tdma_byte |= (uplink_buffer_size & 0x1F);
    }
    
    /* Add sync bit if this is a sync frame */
    if (is_sync) {
        tdma_byte |= 0x20;
    }
    
    /* Add parity bit (bit 7) */
    uint8_t parity = 0;
    for (int i = 0; i < 7; i++) {
        if (tdma_byte & (1 << i)) {
            parity ^= 1;
        }
    }
    tdma_byte |= (parity << 7);
    
    return tdma_byte;
}

/**
 * @brief Get TDMA statistics
 * @param sync_count Pointer to store sync count
 * @param timeout_count Pointer to store timeout count
 */
void TDMATask_GetStats(uint32_t *sync_count, uint32_t *timeout_count)
{
    if (sync_count) {
        *sync_count = tdma_sync_count;
    }
    if (timeout_count) {
        *timeout_count = tdma_timeout_count;
    }
}

/**
 * @brief Initialize NULL frame transmission
 * @param size Frame size in bytes
 */
void TDMA_NULL_frame_init(int size)
{
    // TODO: Implement NULL frame initialization
    // This sets up periodic NULL frame transmission for clients
    // without valid data to send, to maintain TDMA synchronization
}

/**
 * @brief Initialize timing advance for a client
 * @param client_ID Client ID (0-15)
 * @param TA_input Timing advance input value
 */
void TDMA_init_TA(uint8_t client_ID, int TA_input)
{
    if (client_ID < RADIO_ADDR_TABLE_SIZE) {
        TDMA_table_TA[client_ID] = TA_input * 10;  // Scale TA value
    }
}
