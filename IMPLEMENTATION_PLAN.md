# NPR-70 FreeRTOS Port - Implementation Plan

## Executive Summary

This document provides a detailed, step-by-step implementation plan for porting the NPR-70 modem firmware from mbed OS to FreeRTOS 11.x LTS with STM32 HAL drivers.

**Estimated Total Effort:** 40-60 hours
**Target Completion:** Phased approach over 2-4 weeks

---

## Phase 1: Foundation Setup (4-6 hours)

### Task 1.1: Download Required Sources ✅ NEXT

**Objective:** Obtain FreeRTOS kernel and STM32 HAL drivers

**Steps:**
1. Download FreeRTOS Kernel LTS 11.1.0
   ```bash
   cd Middleware/
   git clone --branch V11.1.0 --depth 1 \
     https://github.com/FreeRTOS/FreeRTOS-Kernel.git FreeRTOS
   cd FreeRTOS
   # Remove unnecessary files
   rm -rf .github Demo docs
   ```

2. Download STM32CubeL4
   ```bash
   # Option A: Using git
   cd ../../Drivers/
   git clone --depth 1 \
     https://github.com/STMicroelectronics/STM32CubeL4.git temp
   cp -r temp/Drivers/STM32L4xx_HAL_Driver .
   cp -r temp/Drivers/CMSIS .
   rm -rf temp
   
   # Option B: Manual download from ST website
   # https://www.st.com/en/embedded-software/stm32cubel4.html
   # Extract and copy HAL_Driver and CMSIS to Drivers/
   ```

3. Verify file structure
   ```
   Middleware/FreeRTOS/
   ├── Source/
   │   ├── tasks.c
   │   ├── queue.c
   │   ├── portable/GCC/ARM_CM4F/
   │   └── ...
   └── include/
   
   Drivers/
   ├── STM32L4xx_HAL_Driver/
   │   ├── Inc/
   │   └── Src/
   └── CMSIS/
       ├── Include/
       └── Device/ST/STM32L4xx/
   ```

**Deliverable:** Downloaded source trees

---

### Task 1.2: Create Linker Script and Startup Code

**Objective:** STM32L432KC-specific linker script and startup assembly

**Files to Create:**

1. **STM32L432KC.ld** (linker script)
   - Based on BUILD/NPR_14.link_script.ld
   - Adjusted for FreeRTOS heap placement
   - Reserve last 2KB flash for configuration

2. **startup_stm32l432xx.s** (startup assembly)
   - Copy from CMSIS and modify for FreeRTOS
   - Update vector table with FreeRTOS handlers

3. **system_stm32l4xx.c** (system init)
   - Clock configuration: 80 MHz from MSI via PLL
   - Copy from CMSIS template and customize

**Deliverable:** Bootable system initialization

---

### Task 1.3: Create Build System

**Objective:** Makefile or CMake for compilation

**Option A: Modernized Makefile** (Recommended - familiar)
```makefile
# Variables
PROJECT = npr70-freertos
DEVICE = STM32L432xx
CORE = cortex-m4

# Paths
FREERTOS_DIR = Middleware/FreeRTOS
HAL_DIR = Drivers/STM32L4xx_HAL_Driver
CMSIS_DIR = Drivers/CMSIS

# Sources (auto-discover)
C_SOURCES = $(wildcard Core/Src/*.c)
C_SOURCES += $(wildcard Application/*/*.c)
C_SOURCES += $(FREERTOS_DIR)/Source/*.c
C_SOURCES += $(FREERTOS_DIR)/Source/portable/GCC/ARM_CM4F/*.c
C_SOURCES += $(FREERTOS_DIR)/Source/portable/MemMang/heap_4.c
C_SOURCES += $(HAL_DIR)/Src/stm32l4xx_hal*.c

# Include paths
INCLUDES = -ICore/Inc
INCLUDES += -IApplication/Radio
INCLUDES += -I$(FREERTOS_DIR)/include
INCLUDES += -I$(FREERTOS_DIR)/Source/portable/GCC/ARM_CM4F
INCLUDES += -I$(HAL_DIR)/Inc
INCLUDES += -I$(CMSIS_DIR)/Include
INCLUDES += -I$(CMSIS_DIR)/Device/ST/STM32L4xx/Include

# Compiler flags
CFLAGS = -mcpu=$(CORE) -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS += -D$(DEVICE) -DUSE_HAL_DRIVER
CFLAGS += -Os -g -Wall -ffunction-sections -fdata-sections
CFLAGS += $(INCLUDES)

# Linker flags
LDFLAGS = -T STM32L432KC.ld
LDFLAGS += -specs=nano.specs -lc -lm -lnosys
LDFLAGS += -Wl,--gc-sections -Wl,-Map=$(PROJECT).map

# Targets
all: $(PROJECT).elf $(PROJECT).bin $(PROJECT).hex

# ... (rest of makefile)
```

**Option B: CMake** (More modern, better dependency tracking)
```cmake
cmake_minimum_required(VERSION 3.15)
project(npr70-freertos C ASM)

set(CMAKE_C_STANDARD 11)
set(MCU_FAMILY STM32L4xx)
set(MCU_MODEL STM32L432xx)

# ... (rest of CMake config)
```

**Deliverable:** Working build system

---

## Phase 2: Core System Bring-up (6-8 hours)

### Task 2.1: Implement main.c

**Objective:** Entry point, HAL init, task creation

**File:** `Core/Src/main.c`

```c
#include "main.h"
#include "npr_types.h"

/* Global handles */
TIM_HandleTypeDef htim2;
SPI_HandleTypeDef hspi1, hspi2;
UART_HandleTypeDef huart2;

/* FreeRTOS objects */
EventGroupHandle_t xSystemEvents;
SemaphoreHandle_t xSPI1_Mutex, xSPI2_Mutex, xPrintf_Mutex;

/* Queues */
QueueHandle_t xQueueRadioToEth;
QueueHandle_t xQueueEthToRadio;
QueueHandle_t xQueueRadioISR;

/* Hardware contexts */
SI4463_Context_t g_si4463;
W5500_Context_t g_w5500;
SystemConfig_t g_config;

int main(void) {
    /* Reset all peripherals, init Flash interface and Systick */
    HAL_Init();
    
    /* Configure system clock: 80MHz */
    SystemClock_Config();
    
    /* Initialize peripherals */
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_SPI2_Init();
    MX_USART2_UART_Init();
    MX_TIM2_Init();  /* TDMA microsecond timer */
    MX_TIM3_Init();  /* Runtime stats timer */
    
    /* Create FreeRTOS synchronization objects */
    xSystemEvents = xEventGroupCreate();
    xSPI1_Mutex = xSemaphoreCreateMutex();
    xSPI2_Mutex = xSemaphoreCreateMutex();
    xPrintf_Mutex = xSemaphoreCreateMutex();
    
    /* Create queues */
    xQueueRadioToEth = xQueueCreate(16, sizeof(RadioPacket_t));
    xQueueEthToRadio = xQueueCreate(16, sizeof(EthPacket_t));
    xQueueRadioISR = xQueueCreate(8, sizeof(RadioISREvent_t));
    
    /* Load configuration from flash */
    config_flash_load(&g_config);
    
    /* Create application tasks */
    vCreateApplicationTasks();
    
    /* Start scheduler */
    vTaskStartScheduler();
    
    /* Should never reach here */
    Error_Handler();
}
```

**Deliverable:** Compiling main.c

---

### Task 2.2: Implement HAL MSP Callbacks

**Objective:** Low-level peripheral initialization

**File:** `Core/Src/stm32l4xx_hal_msp.c`

Key functions:
- `HAL_SPI_MspInit()` - Configure SPI1/SPI2 pins, clocks, DMA
- `HAL_UART_MspInit()` - Configure UART2 pins, clocks
- `HAL_TIM_Base_MspInit()` - Configure TIM2/TIM3 clocks
- GPIO pin muxing and alternate functions

**Deliverable:** Peripheral initialization callbacks

---

### Task 2.3: Implement Interrupt Handlers

**Objective:** ISR vectors and handlers

**File:** `Core/Src/stm32l4xx_it.c`

```c
/* SI4463 interrupt - highest priority */
void EXTI3_IRQHandler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    RadioISREvent_t event;
    
    if (__HAL_GPIO_EXTI_GET_IT(SI4463_INT_PIN)) {
        __HAL_GPIO_EXTI_CLEAR_IT(SI4463_INT_PIN);
        
        event.timestamp_us = HAL_GetUsTick();
        event.event_type = RADIO_ISR_EVENT_PENDING;
        
        xQueueSendFromISR(xQueueRadioISR, &event, &xHigherPriorityTaskWoken);
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* W5500 interrupt */
void EXTI9_5_IRQHandler(void) {
    // Similar pattern
}

/* FreeRTOS handlers */
void SysTick_Handler(void) {
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

void PendSV_Handler(void) {
    xPortPendSVHandler();
}

void SVC_Handler(void) {
    vPortSVCHandler();
}
```

**Deliverable:** Working interrupt routing

---

## Phase 3: Hardware Drivers (10-14 hours)

### Task 3.1: Port SI4463 Driver

**Objective:** Radio transceiver driver using HAL SPI

**Files:** 
- `Application/Drivers/si4463_driver.c`
- `Application/Drivers/si4463_driver.h`

**Key Functions to Port:**
1. `SI4463_Init()` - Initialize chip, load configuration
2. `SI4463_SendCommand()` - Send SPI command
3. `SI4463_CTS_ReadAnswer()` - Wait for CTS and read response
4. `SI4463_FIFO_Read()` / `SI4463_FIFO_Write()` - FIFO access
5. `SI4463_StartRX()` / `SI4463_StartTX()` - State transitions
6. `SI4463_ReadFRR()` - Fast response registers
7. `SI4463_ConfigureFromH()` - Load radio config arrays

**Changes from mbed:**
```cpp
// BEFORE (mbed)
SI4463->cs->write(0);
SI4463->spi->transfer_2(cmd, 1, rx, 1);
SI4463->cs->write(1);

// AFTER (HAL)
HAL_GPIO_WritePin(g_si4463.cs_port, g_si4463.cs_pin, GPIO_PIN_RESET);
HAL_SPI_TransmitReceive(g_si4463.hspi, cmd, rx, 1, 100);
HAL_GPIO_WritePin(g_si4463.cs_port, g_si4463.cs_pin, GPIO_PIN_SET);
```

**Critical:** Maintain exact timing for TDMA - verify with logic analyzer

**Deliverable:** Working SI4463 driver, can read version and configure chip

---

### Task 3.2: Port W5500 Driver

**Objective:** Ethernet controller driver

**Files:**
- `Application/Drivers/w5500_driver.c`
- `Application/Drivers/w5500_driver.h`

**Key Functions:**
1. `W5500_Init()` - Reset and configure chip
2. `W5500_ReadByte()` / `W5500_WriteByte()` - Register access
3. `W5500_ReadLong()` / `W5500_WriteLong()` - Bulk data transfer
4. `W5500_SocketInit()` - Configure sockets (RAW, UDP, TCP)
5. `W5500_GetRxSize()` / `W5500_GetTxFreeSize()` - Buffer status
6. `W5500_RecvData()` / `W5500_SendData()` - Packet transfer

**Deliverable:** Working W5500 driver, can ping the device

---

### Task 3.3: Port External SRAM Driver

**Objective:** SPI SRAM for packet buffering

**Files:**
- `Application/Drivers/ext_sram.c`
- `Application/Drivers/ext_sram.h`

**Key Functions:**
1. `ExtSRAM_Detect()` - Presence detection
2. `ExtSRAM_Write()` / `ExtSRAM_Read()` - Data access
3. `ExtSRAM_SetMode()` - Operating mode configuration

**Note:** Shares SPI2 with W5500 - must use `xSPI2_Mutex`

**Deliverable:** Working SRAM driver with mutex protection

---

### Task 3.4: Implement Microsecond Timer for TDMA

**Objective:** High-resolution timestamp source

**File:** `Core/Src/main.c` (inline functions)

```c
/* TIM2 configured as 32-bit upcounter @ 1MHz (1µs tick) */
void MX_TIM2_Init(void) {
    __HAL_RCC_TIM2_CLK_ENABLE();
    
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 79;  /* 80MHz / 80 = 1MHz */
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFF;  /* 32-bit wrap */
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    
    HAL_TIM_Base_Init(&htim2);
    HAL_TIM_Base_Start(&htim2);
}

uint32_t HAL_GetUsTick(void) {
    return __HAL_TIM_GET_COUNTER(&htim2);
}

void delay_us(uint32_t us) {
    uint32_t start = HAL_GetUsTick();
    while ((HAL_GetUsTick() - start) < us);
}
```

**Deliverable:** Verified µs timing accuracy (use oscilloscope)

---

## Phase 4: Radio Task Implementation (12-16 hours)

### Task 4.1: Radio ISR Handler Task

**Objective:** Deferred interrupt processing from SI4463

**File:** `Application/Radio/radio_task.c`

```c
void vRadioISRHandlerTask(void *pvParameters) {
    RadioISREvent_t event;
    
    for (;;) {
        /* Wait for interrupt event */
        if (xQueueReceive(xQueueRadioISR, &event, portMAX_DELAY)) {
            
            /* Process interrupt based on current RX/TX state */
            if (g_si4463.rx_tx_state == 1) {  /* RX mode */
                SI4463_ProcessRxInterrupt(&event);
            } else if (g_si4463.rx_tx_state == 2) {  /* TX mode */
                SI4463_ProcessTxInterrupt(&event);
            }
        }
    }
}
```

**Key Subfunctions:**
- `SI4463_ProcessRxInterrupt()` - Handle RX sync, FIFO full, packet complete
- `SI4463_ProcessTxInterrupt()` - Handle TX FIFO empty, packet sent
- `SI4463_ReadRxFIFO()` - Transfer FIFO data to radio RX buffer
- `SI4463_WriteTxFIFO()` - Fill TX FIFO from buffer

**Deliverable:** ISR handler task receiving and processing interrupts

---

### Task 4.2: TDMA Protocol Implementation

**Objective:** Port TDMA timing and slot allocation

**File:** `Application/Radio/tdma.c`

**Key Functions:**
1. `TDMA_Init()` - Initialize TDMA state machine
2. `TDMA_ByteElaboration()` - Generate TDMA control byte
3. `TDMA_TAMeasure()` - Timing Advance measurement
4. `TDMA_ByteRxInterp()` - Interpret received TDMA byte
5. `TDMA_MasterAllocation()` - Master slot allocation algorithm
6. `TDMA_SlaveTimeout()` - Slave synchronization timeout

**Critical Timing:**
- Use `HAL_GetUsTick()` for all TDMA timestamps
- Maintain exact timing from mbed version
- Verify with scope: slot boundaries, TA measurements

**Deliverable:** Working TDMA master and slave modes

---

### Task 4.3: L1/L2 Radio Protocol

**Objective:** FEC, segmentation, packet routing

**File:** `Application/Radio/l1l2_radio.c`

**Key Functions:**
1. `FEC_Encode()` / `FEC_Decode()` - Forward error correction
2. `SegmentAndPush()` - Segment large packets for radio
3. `RadioRxFIFODequeue()` - Process received radio packets
4. `TxFIFOWrite()` - Queue packets for transmission
5. `ComputeTxBufferSize()` - Available TX buffer space

**Deliverable:** FEC working, packets correctly segmented/reassembled

---

## Phase 5: Ethernet Tasks (8-10 hours)

### Task 5.1: Ethernet RX Task

**Objective:** Receive and process Ethernet packets

**File:** `Application/Ethernet/eth_rx_task.c`

```c
void vEthernetRxTask(void *pvParameters) {
    EthPacket_t packet;
    uint16_t rx_size;
    
    for (;;) {
        /* Check for received data on each socket */
        for (uint8_t sock = 0; sock < 6; sock++) {
            rx_size = W5500_GetRxSize(&g_w5500, sock);
            
            if (rx_size > 0) {
                /* Read packet from W5500 */
                packet.size = W5500_RecvData(&g_w5500, sock, 
                                              packet.data, rx_size);
                packet.socket_id = sock;
                
                /* Process based on socket type */
                switch (sock) {
                    case 0:  /* RAW socket - IPv4 */
                        Eth_ProcessIPv4Packet(&packet);
                        break;
                    case 1:  /* Telnet */
                        // Queue to Telnet task
                        break;
                    case 3:  /* DHCP */
                        // Queue to DHCP task
                        break;
                    case 5:  /* SNMP */
                        // Queue to SNMP task
                        break;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));  /* Poll every 10ms */
    }
}
```

**Deliverable:** Ethernet RX task receiving packets

---

### Task 5.2: Ethernet TX Task

**Objective:** Transmit packets from radio to Ethernet

**File:** `Application/Ethernet/eth_tx_task.c`

```c
void vEthernetTxTask(void *pvParameters) {
    RadioPacket_t radio_pkt;
    EthPacket_t eth_pkt;
    
    for (;;) {
        /* Wait for packets from radio */
        if (xQueueReceive(xQueueRadioToEth, &radio_pkt, portMAX_DELAY)) {
            
            /* Convert radio packet to IPv4 packet */
            IPv4_FromRadio(&radio_pkt, &eth_pkt);
            
            /* Transmit via W5500 */
            W5500_SendData(&g_w5500, 0, eth_pkt.data, eth_pkt.size);
        }
    }
}
```

**Deliverable:** Packets flowing from radio to Ethernet

---

### Task 5.3: IPv4 Packet Handling

**Objective:** IP header parsing and routing

**File:** `Application/Ethernet/ipv4.c`

**Key Functions:**
1. `IPv4_ToRadio()` - Ethernet packet → Radio packet
2. `IPv4_FromRadio()` - Radio packet → Ethernet packet
3. `IPv4_Char2Int()` / `IPv4_Int2Char()` - IP address conversion
4. `LookupClientIDFromIP()` - IP to TDMA client ID mapping

**Deliverable:** Bidirectional IP packet flow

---

## Phase 6: Service Tasks (8-10 hours)

### Task 6.1: DHCP/ARP Service

**File:** `Application/Services/dhcp_arp_task.c`

**Responsibilities:**
- DHCP server for radio clients
- ARP proxy between Ethernet and radio clients
- IP/MAC table management

**Deliverable:** DHCP server assigning IPs to radio clients

---

### Task 6.2: SNMP Agent

**File:** `Application/Services/snmp_task.c`

**Responsibilities:**
- SNMP GET/GETNEXT request processing
- MIB tree traversal
- Statistics reporting

**Deliverable:** SNMP agent responding to queries

---

### Task 6.3: Telnet HMI

**File:** `Application/Services/telnet_task.c`

**Responsibilities:**
- Telnet protocol handling
- Command parsing (`set`, `get`, `stats`, `who`, etc.)
- Configuration management

**Deliverable:** Telnet CLI working, can configure system

---

### Task 6.4: Signaling Task

**File:** `Application/Services/signaling_task.c`

**Responsibilities:**
- Periodic WHOIS broadcasts
- Connection request/response handling
- Client registration

**Deliverable:** Signaling protocol working, clients can connect

---

### Task 6.5: Monitor Task

**File:** `Application/Services/monitor_task.c`

**Responsibilities:**
- Temperature monitoring → recalibration
- LED status updates
- Statistics collection
- Stack usage reporting

**Deliverable:** System monitoring and health checks working

---

## Phase 7: Testing & Optimization (10-15 hours)

### Task 7.1: Unit Testing

Test each driver and module in isolation:
- [ ] SPI communication (loopback test)
- [ ] SI4463 configuration and version read
- [ ] W5500 ping response
- [ ] External SRAM read/write verify
- [ ] TDMA timer accuracy (scope verification)

### Task 7.2: Integration Testing

Test subsystems together:
- [ ] TDMA master mode transmitting
- [ ] TDMA slave mode receiving and measuring TA
- [ ] FEC encode/decode round-trip
- [ ] Ethernet to radio packet routing
- [ ] Radio to Ethernet packet routing

### Task 7.3: Full System Testing

End-to-end scenarios:
- [ ] Two modems: Master-slave TDMA link establishment
- [ ] IP packet ping through radio link
- [ ] Multi-client TDMA with dynamic slot allocation
- [ ] DHCP server assigning IPs
- [ ] Telnet configuration via Ethernet
- [ ] SNMP monitoring
- [ ] 24+ hour stability test

### Task 7.4: Performance Optimization

- [ ] Stack usage profiling (high water mark)
- [ ] CPU usage per task (runtime stats)
- [ ] Memory leak detection (heap fragmentation)
- [ ] Interrupt latency measurement
- [ ] TDMA timing jitter analysis

**Deliverable:** Production-ready firmware

---

## Risk Mitigation

### Risk 1: TDMA Timing Drift

**Mitigation:**
- Use TIM2 as dedicated microsecond counter
- Verify with oscilloscope/logic analyzer
- Compare timestamps with original mbed firmware
- Add runtime timing accuracy monitoring

### Risk 2: SPI Bus Contention

**Mitigation:**
- Strict mutex protection for SPI2 (W5500 + SRAM)
- Priority inheritance enabled
- Minimize critical section duration
- Monitor mutex wait times

### Risk 3: Stack Overflow

**Mitigation:**
- Conservative stack sizing
- Enable stack overflow detection (Method 2)
- Periodic high water mark monitoring
- Use static allocation for critical tasks

### Risk 4: Memory Fragmentation

**Mitigation:**
- Use heap_4.c (coalescence algorithm)
- Preallocate packet buffers
- Avoid frequent malloc/free in tasks
- Monitor heap usage

---

## Success Criteria

### Minimum Viable Product (MVP)
- [ ] FreeRTOS scheduler running
- [ ] All hardware initialized
- [ ] Radio can transmit and receive
- [ ] Ethernet can transmit and receive
- [ ] Basic IP routing between radio and Ethernet
- [ ] System stable for >1 hour

### Full Feature Parity
- [ ] TDMA master and slave modes
- [ ] Multi-client slot allocation
- [ ] DHCP server
- [ ] Telnet CLI with all commands
- [ ] SNMP agent
- [ ] Signaling protocol
- [ ] Configuration storage in flash
- [ ] Temperature recalibration
- [ ] All original features working

### Production Ready
- [ ] 24+ hour stability test passed
- [ ] No memory leaks detected
- [ ] Stack usage <80% on all tasks
- [ ] TDMA timing jitter <50µs
- [ ] Radio link quality equal or better than mbed version
- [ ] Complete documentation
- [ ] Build system fully automated

---

## Timeline Estimate

### Conservative (Assume serial development, 1 person)

| Phase | Tasks | Hours | Calendar Time |
|-------|-------|-------|---------------|
| 1 | Foundation | 6 | 1 day |
| 2 | Core System | 8 | 1 day |
| 3 | Drivers | 14 | 2 days |
| 4 | Radio Tasks | 16 | 2 days |
| 5 | Ethernet Tasks | 10 | 1.5 days |
| 6 | Service Tasks | 10 | 1.5 days |
| 7 | Testing | 15 | 2 days |
| **Total** | | **79 hours** | **~11 days** |

### Realistic (Including debugging, iteration)

Add 30% buffer for unforeseen issues: **~105 hours / 14-16 days**

With part-time effort (4-5 hours/day): **3-4 weeks**

---

## Next Steps

1. ✅ Review this implementation plan
2. ⏳ Download FreeRTOS and STM32 HAL sources
3. ⏳ Create linker script and startup code
4. ⏳ Implement main.c and basic HAL init
5. ⏳ Build and flash "Hello World" FreeRTOS blink
6. ⏳ Continue with driver porting...

---

## Conclusion

This plan provides a structured approach to porting NPR-70 to FreeRTOS while maintaining the critical TDMA timing requirements. The phased approach allows for incremental testing and validation at each stage.

**Key Success Factors:**
- Maintain microsecond-accurate TDMA timing
- Proper mutex protection for shared SPI buses
- Conservative stack sizing with monitoring
- Thorough testing at each phase
- Logic analyzer verification of critical timing

The resulting firmware will be more maintainable, better documented, and built on a modern, actively-maintained RTOS with excellent community support.

---

**Author:** llatva
**Date:** 2025-11-07
**Version:** 1.0
