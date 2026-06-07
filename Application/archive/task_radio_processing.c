/**
  ******************************************************************************
  * @file    task_radio_processing.c
  * @brief   Radio packet processing task (dequeue RX FIFO and process packets)
  ******************************************************************************
  * @attention
  *
  * This task processes received radio packets from the RX FIFO.
  * It handles FEC decoding, packet segmentation/reassembly, and protocol routing.
  * Runs at priority 6 (high priority, below ISR handler).
  *
  ******************************************************************************
  */

#include "task_radio_processing.h"
#include "app_common.h"
#include "fec_codec.h"
#include "w5500_driver.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* Private defines -----------------------------------------------------------*/
#define PROTOCOL_RAW_ETHERNET   0x01
#define PROTOCOL_IPV4_ACCESS    0x02
#define PROTOCOL_SIGNALING      0x1E
#define PROTOCOL_TDMA_ALLOC     0x1F
#define PROTOCOL_NULL           0x00

/* Private variables ---------------------------------------------------------*/
static W5500_Context_t *hw5500 = NULL;

/* Per-client packet reassembly buffers - allocated dynamically to save static RAM */
/* Buffer size depends on SRAM availability: 512B (internal) or 1600B (external) */
uint8_t *ethernet_buffer[RADIO_ADDR_TABLE_SIZE];  /* Pointers to reassembly buffers (lazy-allocated) */
static uint16_t size_received[RADIO_ADDR_TABLE_SIZE];
static uint8_t prev_seg_counter[RADIO_ADDR_TABLE_SIZE];
static uint8_t curr_pkt_counter[RADIO_ADDR_TABLE_SIZE];
/* Last-used timestamp (ms) for idle freeing */
uint32_t buffer_last_used_ms[RADIO_ADDR_TABLE_SIZE];

/* Temporary decode buffer - moved to SRAM2 to save main SRAM1 space (360 bytes saved) */
static uint8_t data_RX[360] PLACE_IN_SRAM2;

/* Statistics */
static uint32_t rx_packet_count = 0;
static uint32_t last_rframe_seen = 0;

/* External variables from other modules (placeholders for now) */
extern uint8_t my_radio_client_ID;  /* Defined in global config */

/* Private function prototypes -----------------------------------------------*/
static void ProcessIPv4Packet(uint8_t client_ID, uint8_t *data, uint16_t size);
static void ProcessSignalingFrame(uint8_t *data, uint16_t size, int32_t TA);
static void ProcessTDMAAllocation(uint8_t *data, uint16_t size);
static void UpdateTDMAByte(uint8_t tdma_byte, uint8_t client_byte, uint8_t protocol_byte, uint32_t frame_timer);
/* Lazy allocation helpers */
static uint8_t *GetOrAllocBuffer(uint8_t LID);
static void FreeIdleBuffers(void);
static int32_t MeasureTDMA_TA(uint32_t frame_timer, uint8_t tdma_byte, uint8_t client_byte, int frame_size);

/**
 * @brief Initialize radio processing task
 * @param w5500_ctx Pointer to W5500 driver context
 */
void RadioProcessingTask_Init(W5500_Context_t *w5500_ctx)
{
    printf("RadioProcessingTask_Init: entry\r\n");
    hw5500 = w5500_ctx;
    
    /* Lazy allocation: don't preallocate buffers here. Initialize control arrays. */
    for (int i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
        ethernet_buffer[i] = NULL;
        size_received[i] = 0;
        prev_seg_counter[i] = 0;
        curr_pkt_counter[i] = 0;
        buffer_last_used_ms[i] = 0;
    }

    printf("RadioProcessingTask_Init: done (lazy allocation enabled)\r\n");
}

/**
 * @brief Radio processing task - dequeue and process RX FIFO packets
 * @param argument Not used
 */
void vRadioProcessingTask(void *argument)
{
    uint8_t tdma_byte, client_byte, protocol_byte;
    uint8_t client_addr, protocol;
    uint8_t is_downlink;
    uint8_t LID;  /* Local ID for buffer indexing */
    uint32_t frame_timer;
    uint8_t RSSI;
    uint16_t rframe_length;
    uint16_t size_w_FEC, size_wo_FEC;
    uint32_t micro_BER;
    int32_t TA_local;
    uint8_t client_ID_filter;
    
    /* Packet segmentation variables */
    uint8_t segmenter_byte;
    uint8_t seg_counter, pkt_counter, is_last_seg;
    uint16_t segment_size;
    
    printf("RadioProcessing task started\r\n");
    
    for (;;) {
        /* Check if there's data in RX FIFO */
        if (RX_FIFO_last_received > RX_FIFO_RD_point) {
            
            /* Read packet header from FIFO (5 bytes: 3-byte timer, 1-byte RSSI, 1-byte length) */
            taskENTER_CRITICAL();
            
            frame_timer = RX_FIFO_ReadByte(RX_FIFO_RD_point & RX_FIFO_MASK);
            RX_FIFO_RD_point++;
            frame_timer |= (RX_FIFO_ReadByte(RX_FIFO_RD_point & RX_FIFO_MASK) << 8);
            RX_FIFO_RD_point++;
            frame_timer |= (RX_FIFO_ReadByte(RX_FIFO_RD_point & RX_FIFO_MASK) << 16);
            RX_FIFO_RD_point++;
            
            RSSI = RX_FIFO_ReadByte(RX_FIFO_RD_point & RX_FIFO_MASK);
            RX_FIFO_RD_point++;
            
            rframe_length = RX_FIFO_ReadByte(RX_FIFO_RD_point & RX_FIFO_MASK);
            RX_FIFO_RD_point++;
            
            /* Read TDMA byte and peek at client/protocol bytes */
            tdma_byte = RX_FIFO_ReadByte(RX_FIFO_RD_point & RX_FIFO_MASK);
            RX_FIFO_RD_point++;
            
            /* Peek at client and protocol bytes (they're inside FEC-encoded data) */
            client_byte = RX_FIFO_ReadByte(RX_FIFO_RD_point & RX_FIFO_MASK);
            protocol_byte = RX_FIFO_ReadByte((RX_FIFO_RD_point + 1) & RX_FIFO_MASK);
            
            /* Calculate FEC-encoded data size (subtract TDMA byte) */
            size_w_FEC = rframe_length - 1;
            
            taskEXIT_CRITICAL();
            
            /* Extract flags */
            is_downlink = tdma_byte & 0x40;
            
            /* TDMA timing measurement (master mode only) */
            TA_local = 0;
            if (is_TDMA_master) {
                TA_local = MeasureTDMA_TA(frame_timer, tdma_byte, client_byte, size_w_FEC);
                
                /* Check for timeout and prepare TX if needed */
                uint32_t current_time = GetMicrosecondTimer();
                if ((current_time - last_rframe_seen) > 2000000) {  /* 2 second timeout */
                    last_rframe_seen = current_time;
                    /* TODO: Trigger TX preparation via queue to TDMA task */
                }
            }
            last_rframe_seen = GetMicrosecondTimer();
            
            /* FEC decode - reads from RX_FIFO_data at RX_FIFO_RD_point, outputs to data_RX */
            size_wo_FEC = FEC_Decode(data_RX, size_w_FEC, &micro_BER);
            
            /* Process valid decoded packets */
            if (size_wo_FEC > 0) {
                client_addr = data_RX[0] & 0x7F;  /* 7-bit client address */
                protocol = data_RX[1];
                
                /* Determine local ID for buffer indexing */
                if (is_TDMA_master) {
                    LID = client_addr;  /* Each client has own buffer */
                } else {
                    LID = 0;  /* Single buffer for client mode */
                }
                
                /* Client ID filter */
                client_ID_filter = 0;
                if (is_TDMA_master) {
                    /* Master: accept uplink from registered clients */
                    if (!is_downlink && client_addr < RADIO_ADDR_TABLE_SIZE) {
                        if (CONF_radio.addr_table_status[client_addr]) {
                            client_ID_filter = 1;
                        }
                    }
                    /* Also accept discovery frames (0x7E) */
                    if (!is_downlink && client_addr == 0x7E) {
                        client_ID_filter = 1;
                    }
                } else {
                    /* Client: accept downlink to us or broadcast */
                    if ((client_addr == my_radio_client_ID || client_addr == 0x7F) && is_downlink) {
                        client_ID_filter = 1;
                    }
                }
                
                /* Process packet based on protocol and filter */
                if (client_ID_filter && LID < RADIO_ADDR_TABLE_SIZE) {
                    
                    switch (protocol) {
                        
                        case PROTOCOL_IPV4_ACCESS:  /* 0x02 - IPv4 packet */
                            segment_size = size_wo_FEC - 3;
                            segmenter_byte = data_RX[2];
                            pkt_counter = (segmenter_byte & 0xF0) >> 4;
                            is_last_seg = segmenter_byte & 0x08;
                            seg_counter = segmenter_byte & 0x07;
                            
                            if (seg_counter == 0) {
                                /* First segment - allocate buffer lazily if needed */
                                uint8_t *buf = GetOrAllocBuffer(LID);
                                if (buf == NULL) {
                                    /* Couldn't allocate - drop packet */
                                    size_received[LID] = 0;
                                } else {
                                    curr_pkt_counter[LID] = pkt_counter;
                                    memcpy(buf + 14, data_RX + 3, segment_size);
                                    size_received[LID] = segment_size;
                                    buffer_last_used_ms[LID] = xTaskGetTickCount() * portTICK_PERIOD_MS;
                                }
                            } else if ((seg_counter == (prev_seg_counter[LID] + 1)) && 
                                       (pkt_counter == curr_pkt_counter[LID])) {
                                /* Continuation segment */
                                uint8_t *buf = ethernet_buffer[LID];
                                if (buf != NULL) {
                                    memcpy(buf + size_received[LID] + 14, 
                                           data_RX + 3, segment_size);
                                    size_received[LID] += segment_size;
                                    buffer_last_used_ms[LID] = xTaskGetTickCount() * portTICK_PERIOD_MS;
                                } else {
                                    /* Missing buffer - continuity broken */
                                    size_received[LID] = 0;
                                }
                            } else {
                                /* Continuity error - reset */
                                size_received[LID] = 0;
                            }

                            prev_seg_counter[LID] = seg_counter;

                            if (is_last_seg && size_received[LID] > 0) {
                                /* Complete packet - process it */
                                ProcessIPv4Packet(LID, ethernet_buffer[LID], size_received[LID] + 14);
                                rx_packet_count++;
                                buffer_last_used_ms[LID] = xTaskGetTickCount() * portTICK_PERIOD_MS;
                            }
                            break;
                            
                        case PROTOCOL_SIGNALING:  /* 0x1E */
                            ProcessSignalingFrame(data_RX, size_wo_FEC, TA_local);
                            break;
                            
                        case PROTOCOL_TDMA_ALLOC:  /* 0x1F - TDMA allocation (client only) */
                            if (!is_TDMA_master) {
                                ProcessTDMAAllocation(data_RX, size_wo_FEC);
                            }
                            break;
                            
                        case PROTOCOL_NULL:  /* 0x00 - Null frame */
                            /* Nothing to do */
                            break;
                            
                        default:
                            /* Unknown protocol */
                            break;
                    }
                }
            }
            
            /* Update TDMA state machine */
            UpdateTDMAByte(tdma_byte, client_byte, protocol_byte, frame_timer);
            
        } else {
            /* No data in FIFO - sleep briefly and free idle buffers occasionally */
            vTaskDelay(pdMS_TO_TICKS(1));
            FreeIdleBuffers();
        }
    }
}

/**
 * @brief Process IPv4 packet from radio - forward to Ethernet
 * @param client_ID Client ID (for buffer management)
 * @param data Ethernet frame data (includes 14-byte header space)
 * @param size Total size including header
 */
static void ProcessIPv4Packet(uint8_t client_ID, uint8_t *data, uint16_t size)
{
    EthernetPacket_t eth_packet;
    
    /* Validate size against active buffer configuration */
    uint16_t max_size = GetActiveEthernetPacketDataSize();
    if (size <= max_size) {
        eth_packet.socket = 0;  /* RAW socket */
        eth_packet.length = size;
        memcpy(eth_packet.data, data, size);
        
        /* Send to Ethernet TX queue */
        xQueueSend(xEthernetTxQueue, &eth_packet, pdMS_TO_TICKS(10));
    } else {
        /* Packet too large for current buffer configuration */
        printf("ProcessIPv4Packet: packet size %u exceeds max %u (dropped)\r\n", size, max_size);
    }
}

/**
 * @brief Process signaling frame
 * @param data Frame data
 * @param size Frame size
 * @param TA Timing advance measurement
 */
static void ProcessSignalingFrame(uint8_t *data, uint16_t size, int32_t TA)
{
    /* TODO: Forward to signaling task via queue or process directly
     * Signaling handles client registration, keep-alive, etc.
     */
}

/**
 * @brief Process TDMA allocation frame (client mode)
 * @param data Frame data
 * @param size Frame size
 */
static void ProcessTDMAAllocation(uint8_t *data, uint16_t size)
{
    /* TODO: Forward to TDMA task for slot allocation processing
     * This tells the client when it can transmit
     */
}

/**
 * @brief Update TDMA state based on received TDMA byte
 * @param tdma_byte TDMA control byte
 * @param client_byte Client ID byte
 * @param protocol_byte Protocol byte
 * @param frame_timer Frame reception timestamp
 */
static void UpdateTDMAByte(uint8_t tdma_byte, uint8_t client_byte, uint8_t protocol_byte, uint32_t frame_timer)
{
    /* Check parity bits */
    if (!parity_bit_check[tdma_byte]) {
        return;  /* Parity error */
    }
    
    uint8_t is_downlink = tdma_byte & 0x40;
    uint8_t is_sync = tdma_byte & 0x20;
    
    /* Update TDMA slave timing (client mode) */
    if (!is_TDMA_master && is_downlink && is_sync) {
        /* This is a master sync frame - update our timing reference */
        taskENTER_CRITICAL();
        TDMA_slave_last_master_top = frame_timer;
        taskEXIT_CRITICAL();
    }
    
    /* TODO: Full TDMA state machine update
     * - Frame counter tracking
     * - Multi-frame synchronization
     * - Slot allocation
     */
}

/**
 * @brief Measure timing advance for TDMA (master mode only)
 * @param frame_timer Frame reception timestamp
 * @param tdma_byte TDMA control byte
 * @param client_byte Client ID byte
 * @param frame_size Frame size
 * @return Timing advance in microseconds
 */
static int32_t MeasureTDMA_TA(uint32_t frame_timer, uint8_t tdma_byte, uint8_t client_byte, int frame_size)
{
    int32_t measured_offset = 0x7FFF;
    int32_t TA_answer = 0x7FFF;
    uint8_t client_ID;
    uint8_t is_downlink = tdma_byte & 0x40;
    uint8_t is_sync = tdma_byte & 0x20;
    
    /* Only measure on uplink sync frames with valid parity */
    if (is_sync && !is_downlink && parity_bit_check[tdma_byte] && parity_bit_check[client_byte]) {
        
        client_ID = client_byte & 0x7F;
        
        if (CONF_radio.master_FDD == 1) {
            measured_offset = frame_timer;
        } else {
            measured_offset = frame_timer - TDMA_slave_last_master_top;
        }
        
        /* Frame size compensation */
        if (frame_size < 114) {
            measured_offset += (114 - frame_size) * 0.85;
        }
        
        /* Update TA for this client */
        if (client_ID < RADIO_ADDR_TABLE_SIZE && CONF_radio.addr_table_status[client_ID]) {
            TA_answer = measured_offset - (10 * (int32_t)TDMA_table_TA[client_ID]);
            
            if (TA_answer > -200 && TA_answer < 2000) {
                /* Apply filtering */
                taskENTER_CRITICAL();
                TDMA_table_TA[client_ID] = (int32_t)(0.9f * TDMA_table_TA[client_ID] + 1.0f * TA_answer);
                taskEXIT_CRITICAL();
            }
        }
    }
    
    return TA_answer;
}

/* Attempt to return an existing buffer or allocate one if enough heap is available.
 * Returns NULL if allocation failed.
 */
static uint8_t *GetOrAllocBuffer(uint8_t LID)
{
    if (LID >= RADIO_ADDR_TABLE_SIZE) return NULL;

    if (ethernet_buffer[LID] != NULL) return ethernet_buffer[LID];

    /* Get buffer size based on SRAM configuration */
    uint16_t buf_size = GetActiveEthernetPacketDataSize();
    unsigned int free_before = (unsigned int)xPortGetFreeHeapSize();
    
    /* Check if we have enough heap (need buf_size + margin) */
    if (free_before <= (buf_size + 200U)) {
        /* Not enough heap to allocate safely */
        printf("GetOrAllocBuffer: insufficient heap (%u bytes) for LID %u (%u bytes needed)\r\n",
               free_before, (unsigned int)LID, (unsigned int)(buf_size + 200U));
        return NULL;
    }

    uint8_t *buf = (uint8_t *)pvPortMalloc(buf_size);
    if (buf == NULL) return NULL;

    memset(buf, 0, buf_size);
    ethernet_buffer[LID] = buf;
    buffer_last_used_ms[LID] = xTaskGetTickCount() * portTICK_PERIOD_MS;
    printf("GetOrAllocBuffer: allocated %u bytes for LID %u at %p (free after: %u)\r\n",
           (unsigned int)buf_size, (unsigned int)LID, (void*)buf, (unsigned int)xPortGetFreeHeapSize());
    return buf;
}

/* Free buffers that haven't been used for IDLE_TIMEOUT_MS milliseconds */
#define IDLE_TIMEOUT_MS 60000  /* 60 seconds */
static void FreeIdleBuffers(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    for (int i = 0; i < RADIO_ADDR_TABLE_SIZE; i++) {
        if (ethernet_buffer[i] != NULL) {
            if (now > buffer_last_used_ms[i] && (now - buffer_last_used_ms[i]) > IDLE_TIMEOUT_MS) {
                vPortFree(ethernet_buffer[i]);
                printf("FreeIdleBuffers: freed buffer %d at %p\r\n", i, (void*)ethernet_buffer[i]);
                ethernet_buffer[i] = NULL;
                size_received[i] = 0;
                prev_seg_counter[i] = 0;
                curr_pkt_counter[i] = 0;
                buffer_last_used_ms[i] = 0;
            }
        }
    }
}
