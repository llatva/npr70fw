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
#include "fec_codec.h"
#include "si4463_driver.h"
#include "watchdog.h"
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

/* Private function prototypes -----------------------------------------------*/
static void TDMA_MasterAllocation(void);
static void TDMA_MasterAllocationSlow(void);
static uint32_t ComputeTxBufferSize(void);
static uint8_t TDMA_GenerateByte(uint8_t is_sync);

/**
 * @brief Compute TX buffer size (placeholder)
 * @return Number of frames in TX buffer
 */
static uint32_t ComputeTxBufferSize(void)
{
    /* TODO: Implement actual TX buffer size calculation
     * This should return the number of frames waiting in the TX buffer
     * For now, return 0 (no data pending)
     */
    return 0;
}

/**
 * @brief TDMA master allocation slow (move clients between fast/slow slots)
 */
static void TDMA_MasterAllocationSlow(void)
{
    int i;
    
    for (i = 0; i < MAX_TDMA_CLIENTS; i++) {
        /* If client hasn't used uplink for a while, move to slow slot */
        if (CONF_radio.addr_table_status[i] && tdma_table_uplink_usage[i] == 0) {
            tdma_table_is_fast[i] = 0;
        }
        
        /* If client is inactive, reset to defaults */
        if (CONF_radio.addr_table_status[i] <= 0) {
            tdma_table_uplink_st[i] = 0;
            tdma_table_uplink_usage[i] = 32;
            tdma_table_is_fast[i] = 1;
        }
    }
}

/**
 * @brief TDMA master allocation algorithm
 * 
 * Distributes uplink slots to clients based on their reported buffer sizes.
 * Creates TDMA allocation frame and writes it to TX_TDMA_intern_data buffer.
 */
static void TDMA_MasterAllocation(void)
{
    int i;
    int allocated_slots;
    int nb_fast_clients;
    uint32_t loc_time_offset;
    int32_t local_TA;
    uint32_t downlink_buffer_size;
    uint8_t TDMA_alloc_frame_raw[150];
    uint8_t rframe_length;
    uint8_t loc_client_needs[MAX_TDMA_CLIENTS];
    uint8_t loc_master_needs;
    uint8_t remaining_needs;
    int size_wo_FEC, size_w_FEC;
    
    /* Decrement uplink usage counters */
    for (i = 0; i < MAX_TDMA_CLIENTS; i++) {
        if (CONF_radio.addr_table_status[i] && tdma_table_uplink_usage[i] > 0) {
            tdma_table_is_fast[i] = 1;
            tdma_table_uplink_usage[i]--;
        }
    }
    
    /* Run slow allocation every 4 frames */
    if ((tdma_frame_nb & 0x3) == 0) {
        TDMA_MasterAllocationSlow();
    }
    
    /* 1) Master computes its own downlink buffer size */
    downlink_buffer_size = ComputeTxBufferSize();
    if (downlink_buffer_size > 30) {
        downlink_buffer_size = 30;
    }
    
    /* 2) If no TDMA uplink received from client, lower its need */
    for (i = 0; i < MAX_TDMA_CLIENTS; i++) {
        if (CONF_radio.addr_table_status[i] && !tdma_table_up2date[i] && tdma_table_uplink_st[i] > 0) {
            tdma_table_uplink_st[i]--;
        }
        tdma_table_up2date[i] = 0;
    }
    
    /* 3) Initialize allocation table */
    /* Master */
    loc_master_needs = downlink_buffer_size;
    master_allocated_slots = 1;
    if (loc_master_needs > 0) {
        loc_master_needs--;
    }
    allocated_slots = 1;  /* At least 1 for master */
    
    /* Clients */
    nb_fast_clients = 0;
    for (i = 0; i < MAX_TDMA_CLIENTS; i++) {
        if (CONF_radio.addr_table_status[i] && tdma_table_is_fast[i]) {
            nb_fast_clients++;
            loc_client_needs[i] = tdma_table_uplink_st[i];
            tdma_table_slots[i] = 1;  /* Allocate 1 slot initially */
            if (loc_client_needs[i] > 0) {
                loc_client_needs[i]--;
            }
            allocated_slots++;
        } else {
            loc_client_needs[i] = 0;
        }
    }
    
    /* 4) First allocation pass - round robin */
    remaining_needs = 1;
    while ((allocated_slots < 15) && (remaining_needs > 0)) {
        /* Master */
        if ((loc_master_needs > 0) && (allocated_slots < 15)) {
            master_allocated_slots++;
            loc_master_needs--;
            allocated_slots++;
        }
        /* Master counts double if more than 1 client */
        if ((loc_master_needs > 0) && (allocated_slots < 15) && (nb_fast_clients > 1)) {
            master_allocated_slots++;
            loc_master_needs--;
            allocated_slots++;
        }
        
        remaining_needs = loc_master_needs;
        
        /* Clients */
        for (i = 0; i < MAX_TDMA_CLIENTS; i++) {
            if (CONF_radio.addr_table_status[i] && tdma_table_is_fast[i] && 
                loc_client_needs[i] > 0 && allocated_slots < 15) {
                tdma_table_slots[i]++;
                loc_client_needs[i]--;
                allocated_slots++;
                remaining_needs += loc_client_needs[i];
            }
        }
    }
    
    /* 5) Second allocation pass - round robin of remaining, even without needs */
    while (allocated_slots < 15) {
        master_allocated_slots++;
        allocated_slots++;
        
        for (i = 0; i < MAX_TDMA_CLIENTS; i++) {
            if (CONF_radio.addr_table_status[i] && tdma_table_is_fast[i] && allocated_slots < 15) {
                tdma_table_slots[i]++;
                allocated_slots++;
            }
        }
    }
    
    /* 6) Timing construction */
    time_max_TX_burst = (CONF_reduced_TDMA_slot_duration + CONF_TDMA_slot_margin) + 
                        (master_allocated_slots * CONF_TDMA_slot_duration) + 
                        ((master_allocated_slots - 1) * CONF_TDMA_slot_margin);
    
    loc_time_offset = time_max_TX_burst + CONF_TDMA_slot_margin + CONF_TR_margain + CONF_TA_margain;
    
    for (i = 0; i < MAX_TDMA_CLIENTS; i++) {
        if (CONF_radio.addr_table_status[i] && tdma_table_is_fast[i]) {
            local_TA = TDMA_table_TA[i];
            if ((local_TA > -2000) && (local_TA < 20000)) {
                tdma_table_offset[i] = (loc_time_offset / 10) - (local_TA / 100);
            } else {
                tdma_table_offset[i] = (loc_time_offset / 10);
            }
            loc_time_offset += (tdma_table_slots[i] * (CONF_TDMA_slot_duration + CONF_TDMA_slot_margin));
        }
    }
    
    /* Multi-frame slots for slow clients */
    TDMA_offset_multi_frame = loc_time_offset / 10;
    
    /* First group (clients 0-3) */
    for (i = 0; i < 4 && i < MAX_TDMA_CLIENTS; i++) {
        if (CONF_radio.addr_table_status[i] && !tdma_table_is_fast[i]) {
            local_TA = TDMA_table_TA[i];
            if ((local_TA > -2000) && (local_TA < 20000)) {
                tdma_table_offset[i] = TDMA_offset_multi_frame - (local_TA / 100);
            } else {
                tdma_table_offset[i] = TDMA_offset_multi_frame;
            }
        }
    }
    
    loc_time_offset += (CONF_TDMA_slot_duration + CONF_TDMA_slot_margin);
    TDMA_offset_multi_frame = loc_time_offset / 10;
    
    /* Second group (clients 4+) */
    for (i = 4; i < MAX_TDMA_CLIENTS; i++) {
        if (CONF_radio.addr_table_status[i] && !tdma_table_is_fast[i]) {
            local_TA = TDMA_table_TA[i];
            if ((local_TA > -2000) && (local_TA < 20000)) {
                tdma_table_offset[i] = TDMA_offset_multi_frame - (local_TA / 100);
            } else {
                tdma_table_offset[i] = TDMA_offset_multi_frame;
            }
        }
    }
    
    /* ** TDMA allocation frame construction ** */
    TDMA_alloc_frame_raw[0] = 0xFF;  /* Broadcast address */
    TDMA_alloc_frame_raw[1] = 0x1F;  /* Protocol = TDMA allocation */
    size_wo_FEC = 2;
    
    /* Add allocation entries for each active client */
    for (i = 0; i < MAX_TDMA_CLIENTS; i++) {
        if (CONF_radio.addr_table_status[i]) {
            if (tdma_table_is_fast[i]) {
                /* Fast slot client */
                TDMA_alloc_frame_raw[size_wo_FEC++] = i;  /* Client ID */
                TDMA_alloc_frame_raw[size_wo_FEC++] = tdma_table_offset[i] & 0xFF;  /* Offset LSB */
                TDMA_alloc_frame_raw[size_wo_FEC++] = (tdma_table_offset[i] >> 8) & 0xFF;  /* Offset MSB */
                TDMA_alloc_frame_raw[size_wo_FEC++] = tdma_table_slots[i] & 0x0F;  /* Slot count */
                TDMA_alloc_frame_raw[size_wo_FEC++] = 0;  /* Multiframe ID (0 = fast) */
            } else {
                /* Slow slot client */
                TDMA_alloc_frame_raw[size_wo_FEC++] = i;  /* Client ID */
                TDMA_alloc_frame_raw[size_wo_FEC++] = tdma_table_offset[i] & 0xFF;  /* Offset LSB */
                TDMA_alloc_frame_raw[size_wo_FEC++] = (tdma_table_offset[i] >> 8) & 0xFF;  /* Offset MSB */
                TDMA_alloc_frame_raw[size_wo_FEC++] = 1;  /* Slot count */
                TDMA_alloc_frame_raw[size_wo_FEC++] = 0x20 + (i & 0x03);  /* Multiframe period=2, ID=i&3 */
            }
        }
    }
    
    /* Discovery slot */
    TDMA_alloc_frame_raw[size_wo_FEC++] = 0x7E;  /* Discovery client ID */
    TDMA_alloc_frame_raw[size_wo_FEC++] = TDMA_offset_multi_frame & 0xFF;  /* Offset LSB */
    TDMA_alloc_frame_raw[size_wo_FEC++] = (TDMA_offset_multi_frame >> 8) & 0xFF;  /* Offset MSB */
    TDMA_alloc_frame_raw[size_wo_FEC++] = 1;  /* Slot count */
    TDMA_alloc_frame_raw[size_wo_FEC++] = 0x23;  /* Multiframe period=2, ID=3 */
    
    /* END marker */
    TDMA_alloc_frame_raw[size_wo_FEC++] = 0xFF;
    
    /* Minimum size */
    if (size_wo_FEC < 66) {
        size_wo_FEC = 66;
    }
    
    /* FEC encode */
    size_w_FEC = FEC_SizeWithEncoding(size_wo_FEC);
    rframe_length = size_w_FEC + 1 - SI4463_OFFSET_SIZE;
    
    /* Build frame in TX_TDMA_intern_data */
    TX_TDMA_intern_data[0] = 0;  /* Timer coarse (not used) */
    TX_TDMA_intern_data[1] = rframe_length;
    TX_TDMA_intern_data[2] = TDMA_GenerateByte(1);  /* TDMA byte with sync */
    
    /* FEC encode the allocation data */
    size_w_FEC = FEC_Encode(TDMA_alloc_frame_raw, TX_TDMA_intern_data + 3, size_wo_FEC);
}

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
        /* Check in with watchdog */
        Watchdog_TaskCheckin(xTaskGetCurrentTaskHandle());
        
        current_time = GetMicrosecondTimer();
        
        if (is_TDMA_master) {
            /* ========== MASTER MODE ========== */
            
            /* Master mode TDMA allocation - called periodically
             * This prepares the TDMA allocation frame for transmission
             */
            if ((current_time - last_check_time) >= conf_tdma_frame_duration) {
                /* Run allocation algorithm */
                TDMA_MasterAllocation();
                
                /* Increment frame counter */
                tdma_frame_nb = (tdma_frame_nb + 1) & 0x1F;
                last_check_time = current_time;
                tdma_sync_count++;
                
                /* TODO-4a: Trigger SI4463 TX preparation here
                 * The allocation frame is now in TX_TDMA_intern_data
                 * Need to schedule TX via timer or direct call
                 */
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
 * @param alloc_data Allocation frame data (starts with client ID)
 * @param size Frame size
 * 
 * Parses TDMA allocation frame from master to extract slot assignment.
 * Format per client: [ID][Offset_LSB][Offset_MSB][Slot_count][Multiframe_info]
 */
void TDMA_ProcessAllocation(const uint8_t *alloc_data, uint16_t size)
{
    static const uint8_t LUT_multif_mask[8] = {0, 1, 3, 7, 15, 31};
    int i;
    uint8_t loc_client_ID;
    uint8_t loc_TDMA_slot_length;
    uint32_t loc_TDMA_offset;
    
    if (!is_TDMA_master && alloc_data != NULL && size > 2) {
        /* Start parsing from byte 2 (skip address and protocol) */
        i = 2;
        loc_client_ID = alloc_data[i];
        
        /* Parse entries until we find our ID or end marker (0xFF) */
        while ((loc_client_ID != 0xFF) && ((i + 5) <= size)) {
            if (loc_client_ID == my_radio_client_ID) {
                /* Found our allocation entry */
                
                /* Extract timing offset (microseconds / 10) */
                loc_TDMA_offset = (alloc_data[i + 1] | (alloc_data[i + 2] << 8)) * 10;
                offset_time_TX_slave = loc_TDMA_offset;
                
                /* Extract slot length (lower 4 bits) */
                loc_TDMA_slot_length = alloc_data[i + 3] & 0x0F;
                time_max_TX_burst = (loc_TDMA_slot_length * CONF_TDMA_slot_duration) + 
                                    ((loc_TDMA_slot_length - 1) * CONF_TDMA_slot_margin);
                
                /* Extract multiframe info */
                my_multiframe_id = alloc_data[i + 4] & 0x0F;  /* Lower 4 bits = ID */
                my_multiframe_mask = (alloc_data[i + 4] >> 4) & 0x0F;  /* Upper 4 bits = period */
                my_multiframe_mask = LUT_multif_mask[my_multiframe_mask];
                
                /* Reset allocation age - we have fresh allocation */
                slave_alloc_rx_age = 0;
                my_client_radio_connexion_state = 2;  /* Connected */
                
                break;
            }
            
            /* Move to next entry (5 bytes per entry) */
            i += 5;
            if (i < size) {
                loc_client_ID = alloc_data[i];
            } else {
                break;
            }
        }
    }
}

/**
 * @brief Generate TDMA byte for transmission
 * @param is_sync Set to 1 for sync frame (first frame in sequence)
 * @return TDMA control byte with parity
 */
static uint8_t TDMA_GenerateByte(uint8_t is_sync)
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
 * 
 * Creates a NULL data frame for clients without valid data to transmit.
 * This maintains TDMA synchronization even when no data is pending.
 */
void TDMA_NULL_frame_init(int size)
{
    uint8_t null_frame[260];
    int size_wo_FEC, size_w_FEC;
    uint8_t rframe_length;
    
    /* Build NULL frame header */
    null_frame[0] = my_radio_client_ID + parity_bit_elab[my_radio_client_ID & 0x7F];  /* Client address with parity */
    null_frame[1] = 0x00;  /* Protocol = NULL frame */
    
    /* Pad with zeros */
    memset(null_frame + 2, 0, size - 2);
    
    size_wo_FEC = size;
    
    /* Calculate FEC-encoded size */
    size_w_FEC = FEC_SizeWithEncoding(size_wo_FEC);
    rframe_length = size_w_FEC + 1 - SI4463_OFFSET_SIZE;
    
    /* Build frame in TX_TDMA_intern_data */
    TX_TDMA_intern_data[0] = 0;  /* Timer (filled later) */
    TX_TDMA_intern_data[1] = rframe_length;
    TX_TDMA_intern_data[2] = TDMA_GenerateByte(1);  /* TDMA byte with sync */
    
    /* FEC encode */
    size_w_FEC = FEC_Encode(null_frame, TX_TDMA_intern_data + 3, size_wo_FEC);
    
    /* Write to SI4463 TX FIFO */
    if (hsi4463 != NULL) {
        uint16_t total_size = size_w_FEC + 3;
        
        /* SI4463_WriteTxFifo accepts max 129 bytes at a time */
        if (total_size <= 129) {
            SI4463_WriteTxFifo(hsi4463, TX_TDMA_intern_data, total_size);
        } else {
            /* Split into multiple writes if needed */
            uint16_t offset = 0;
            while (offset < total_size) {
                uint8_t chunk_size = (total_size - offset) > 129 ? 129 : (total_size - offset);
                SI4463_WriteTxFifo(hsi4463, TX_TDMA_intern_data + offset, chunk_size);
                offset += chunk_size;
            }
        }
        
        /* Start TX transmission */
        SI4463_StartTx(hsi4463, 0, size_w_FEC + 1);  /* Channel 0, size includes TDMA byte */
    }
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
