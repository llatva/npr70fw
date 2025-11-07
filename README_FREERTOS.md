# NPR-70 Modem Firmware - FreeRTOS Port

## Overview

This is a complete rewrite of the NPR-70 modem firmware using FreeRTOS 11.x LTS and STM32 HAL drivers, replacing the mbed OS implementation.

**Target Hardware:** STM32L432KC (Cortex-M4F @ 80MHz, 256KB Flash, 64KB RAM)

**Key Components:**
- **Radio:** SI4463 GMSK transceiver (430-440 MHz)
- **Ethernet:** Wiznet W5500 (hardware TCP/IP stack)
- **External SRAM:** SPI SRAM for packet buffering
- **Protocol:** Custom TDMA (Time Division Multiple Access) for radio coordination

---

## Architecture

### Thread Architecture

The firmware uses a multi-threaded architecture with priority-based scheduling optimized for real-time TDMA timing requirements:

```
Priority 7 (HIGHEST):  Radio ISR Handler Task
                       ↓ (deferred interrupt processing)
Priority 6:            Radio Processing Task
                       ↓ (packet queues)
Priority 5:            Ethernet RX Task
Priority 4:            Ethernet TX Task
Priority 3:            DHCP/ARP Service, SNMP Agent
Priority 2:            Telnet HMI, Signaling Service
Priority 1:            System Monitor Task
Priority 0 (LOWEST):   Idle Task (LED updates, statistics)
```

### Inter-Task Communication

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│  SI4463 ISR │────────>│ Radio ISR    │────────>│ Radio RX    │
│  (Hardware) │  Queue  │ Handler Task │  Queue  │ Processing  │
└─────────────┘         └──────────────┘         └─────────────┘
                                                         │
                                                         ↓ Queue
                                                  ┌─────────────┐
┌─────────────┐         ┌──────────────┐         │ Ethernet TX │
│  W5500 ISR  │────────>│ Ethernet RX  │←────────│    Task     │
│  (Hardware) │  Queue  │     Task     │         └─────────────┘
└─────────────┘         └──────────────┘                ↑
                               │                        │
                               ↓ Queue                  │
                        ┌─────────────┐                 │
                        │  Radio TX   │─────────────────┘
                        │  Processing │      Queue
                        └─────────────┘
```

### Synchronization Primitives

- **Mutexes:**
  - `xSPI1_Mutex`: SI4463 SPI bus access
  - `xSPI2_Mutex`: W5500 + External SRAM shared SPI bus
  - `xPrintf_Mutex`: Thread-safe debug output

- **Queues:**
  - `xQueueRadioToEth`: Radio RX → Ethernet TX (16 packets)
  - `xQueueEthToRadio`: Ethernet RX → Radio TX (16 packets)
  - `xQueueRadioISR`: SI4463 ISR events (8 events)
  - `xQueueW5500ISR`: W5500 ISR events (4 events)

- **Event Groups:**
  - `xSystemEvents`: System-wide state flags (radio configured, link up, etc.)

---

## Directory Structure

```
npr70fw-freertos/
├── Core/
│   ├── Inc/
│   │   ├── main.h                  # Main configuration
│   │   ├── FreeRTOSConfig.h        # FreeRTOS configuration
│   │   ├── npr_types.h             # Common types and structures
│   │   └── stm32l4xx_it.h          # Interrupt handlers
│   └── Src/
│       ├── main.c                  # Main entry, initialization
│       ├── freertos.c              # Task creation and startup
│       ├── stm32l4xx_it.c          # Interrupt service routines
│       ├── stm32l4xx_hal_msp.c     # HAL callbacks
│       ├── system_stm32l4xx.c      # System initialization
│       └── syscalls.c              # Newlib syscalls
│
├── Application/
│   ├── Radio/
│   │   ├── radio_task.c            # Radio ISR handler and processing
│   │   ├── si4463_driver.c         # SI4463 low-level driver
│   │   ├── tdma.c                  # TDMA protocol implementation
│   │   ├── l1l2_radio.c            # L1/L2 radio protocol
│   │   └── signaling.c             # Radio signaling protocol
│   │
│   ├── Ethernet/
│   │   ├── eth_rx_task.c           # Ethernet RX processing
│   │   ├── eth_tx_task.c           # Ethernet TX processing
│   │   ├── w5500_driver.c          # W5500 low-level driver
│   │   ├── ipv4.c                  # IPv4 packet handling
│   │   └── dhcp_arp.c              # DHCP server and ARP proxy
│   │
│   ├── Services/
│   │   ├── snmp_task.c             # SNMP agent
│   │   ├── telnet_task.c           # Telnet HMI server
│   │   ├── signaling_task.c        # Periodic signaling
│   │   └── monitor_task.c          # System monitoring
│   │
│   └── Drivers/
│       ├── ext_sram.c              # External SPI SRAM driver
│       └── config_flash.c          # Configuration storage in flash
│
├── Drivers/
│   ├── STM32L4xx_HAL_Driver/       # STM32 HAL (from ST)
│   └── CMSIS/                      # CMSIS headers (from ARM/ST)
│
├── Middleware/
│   └── FreeRTOS/                   # FreeRTOS kernel source
│       ├── Source/
│       │   ├── tasks.c
│       │   ├── queue.c
│       │   ├── list.c
│       │   ├── timers.c
│       │   ├── event_groups.c
│       │   └── portable/
│       │       └── GCC/ARM_CM4F/   # Cortex-M4F port
│       └── include/
│
├── Makefile (or CMakeLists.txt)    # Build system
├── STM32L432KC.ld                  # Linker script
└── README.md                       # This file
```

---

## Key Design Decisions

### 1. Timing Critical Path - TDMA

The TDMA protocol requires **microsecond precision** for:
- Slot synchronization
- Timing Advance (TA) measurements
- Packet scheduling

**Implementation:**
- TIM2 configured as 32-bit microsecond timer @ 80MHz
- SI4463 interrupt has highest priority (7) with minimal latency
- Deferred interrupt processing pattern: ISR → Queue → High-priority task
- Critical timing sections use `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`

### 2. SPI Bus Sharing

Both W5500 and External SRAM share SPI2:
- **Mutex protection:** `xSPI2_Mutex` prevents concurrent access
- Fast mutex with priority inheritance to avoid priority inversion
- Minimal critical sections to reduce blocking

### 3. Memory Management

**Heap:** 32KB allocated for FreeRTOS (`configTOTAL_HEAP_SIZE`)
- heap_4.c used (coalescence, deterministic)
- Static allocation for critical tasks (radio, ethernet)

**External SRAM:** Used for radio TX buffer overflow
- Reduces internal RAM pressure
- Managed by dedicated driver with mutex protection

### 4. Stack Sizing

Carefully tuned stack sizes based on profiling:
- Radio ISR Handler: 2KB (complex state machine)
- Telnet: 2KB (string processing, sprintf)
- Ethernet RX/TX: 1.5KB each
- Services: 1-1.5KB
- Total: ~12KB for stacks

Remaining RAM (~20KB) for:
- Global variables
- Packet buffers (queues)
- Configuration data

### 5. Watchdog and Fault Handling

- Independent watchdog (IWDG) refreshed by Idle task
- Stack overflow detection enabled (Method 2)
- Hard fault handler captures registers for debugging
- `configASSERT()` halts system with interrupts disabled

---

## Building the Firmware

### Prerequisites

```bash
# Install ARM GCC toolchain
sudo apt-get install gcc-arm-none-eabi

# Install build tools
sudo apt-get install make cmake git
```

### Download Dependencies

#### 1. FreeRTOS Kernel (LTS 11.1.0)
```bash
cd Middleware/
git clone --branch V11.1.0 --depth 1 \
  https://github.com/FreeRTOS/FreeRTOS-Kernel.git FreeRTOS
```

#### 2. STM32 HAL Drivers
```bash
# Download STM32CubeL4 from ST
# Extract to Drivers/STM32L4xx_HAL_Driver/ and Drivers/CMSIS/
```

### Build Commands

#### Using Make:
```bash
make clean
make -j4
make flash  # Flash via ST-Link
```

#### Using CMake:
```bash
mkdir build && cd build
cmake ..
make -j4
make flash
```

---

## Configuration

### Flash Configuration Storage

System configuration is stored in flash memory (last 2KB of flash) and includes:
- Radio parameters (frequency, power, modulation)
- TDMA settings (master/slave, timings)
- Network configuration (IP, MAC, DHCP settings)
- Service enables (Telnet, SNMP, etc.)

### Default Configuration

On first boot or after flash erase, default configuration is loaded:
- **Radio:** 432 MHz, modulation 23, network ID 0, power 100%
- **TDMA:** Slave mode, TDD operation
- **Network:** DHCP off, IP 192.168.1.70, static configuration

### Runtime Configuration

Configuration can be modified via:
1. **Telnet CLI** (port 23) - Full command set
2. **Serial UART** (921600 baud) - Same CLI
3. **SNMP** (read-only for most parameters)

---

## Task Details

### 1. Radio ISR Handler Task (Priority 7)

**Purpose:** Deferred interrupt processing from SI4463
**Stack:** 2KB
**Triggers:** SI4463 hardware interrupt → Queue event

**Responsibilities:**
- Process SI4463 interrupt events (RX sync, FIFO full, packet complete)
- Read FIFO data with precise timing
- Update TDMA timing measurements
- Queue received packets to Radio Processing task

**Critical Timing:** ~10-50µs from interrupt to FIFO read

### 2. Radio Processing Task (Priority 6)

**Purpose:** Radio packet processing and protocol handling
**Stack:** 2KB

**Responsibilities:**
- FEC decoding
- TDMA byte interpretation
- Packet segmentation/reassembly
- Queue packets to Ethernet TX
- Handle TX scheduling and TDMA slot management
- Temperature-based recalibration

### 3. Ethernet RX Task (Priority 5)

**Purpose:** Process incoming Ethernet packets
**Stack:** 1.5KB

**Responsibilities:**
- Poll W5500 for received data (interrupt driven)
- IPv4 header parsing
- ARP/DHCP/ICMP handling
- Route IP packets to radio (queue to Radio TX)
- Socket management

### 4. Ethernet TX Task (Priority 4)

**Purpose:** Transmit packets via W5500
**Stack:** 1.5KB

**Responsibilities:**
- Dequeue packets from Radio RX
- IPv4 header construction
- MAC address resolution (ARP)
- Write to W5500 TX buffers
- Handle flow control

### 5. DHCP/ARP Service (Priority 3)

**Purpose:** DHCP server and ARP proxy
**Stack:** 1KB

**Responsibilities:**
- DHCP server for radio clients
- ARP request/response handling
- IP/MAC table management
- Periodic table cleanup

### 6. SNMP Agent (Priority 3)

**Purpose:** SNMP monitoring interface
**Stack:** 1.5KB

**Responsibilities:**
- SNMP GET/GETNEXT request processing
- MIB tree traversal
- Statistics collection
- Read-only access to configuration

### 7. Telnet HMI Task (Priority 2)

**Purpose:** Command-line interface via Telnet
**Stack:** 2KB

**Responsibilities:**
- Telnet protocol handling
- Command parsing and execution
- Configuration management
- Status display
- Diagnostic commands

### 8. Signaling Task (Priority 2)

**Purpose:** Periodic radio signaling
**Stack:** 1.5KB

**Responsibilities:**
- Send periodic WHOIS frames
- Connection management
- Client registration/deregistration
- Heartbeat monitoring

### 9. Monitor Task (Priority 1)

**Purpose:** System health monitoring
**Stack:** 1KB

**Responsibilities:**
- Temperature monitoring and calibration
- Statistics collection (RSSI, BER, packet counts)
- LED status updates
- Watchdog refresh
- Runtime stack usage reporting

---

## Porting Guide - From mbed to FreeRTOS

### Step 1: HAL Initialization

**mbed:**
```cpp
DigitalOut led(PB_1);
SPI spi(PB_5, PB_4, PB_3);
```

**FreeRTOS + HAL:**
```c
// In main.c initialization
MX_GPIO_Init();
MX_SPI2_Init();

// In task
HAL_GPIO_WritePin(LED_RX_PORT, LED_RX_PIN, GPIO_PIN_SET);
HAL_SPI_Transmit(&hspi2, txbuf, len, HAL_MAX_DELAY);
```

### Step 2: Timing

**mbed:**
```cpp
wait_ms(100);
Timer timer;
timer.start();
uint32_t t = timer.read_us();
```

**FreeRTOS:**
```c
vTaskDelay(pdMS_TO_TICKS(100));  // Task delay
uint32_t t = HAL_GetUsTick();    // Microsecond timestamp
```

### Step 3: Interrupts

**mbed:**
```cpp
InterruptIn int_pin(PA_3);
int_pin.fall(&callback_function);
```

**FreeRTOS:**
```c
// In stm32l4xx_it.c
void EXTI3_IRQHandler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // Handle interrupt, send event to queue
    xQueueSendFromISR(xQueueRadioISR, &event, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### Step 4: Printf

**mbed:**
```cpp
printf("Value: %d\r\n", value);
```

**FreeRTOS (thread-safe):**
```c
safe_printf("Value: %d\r\n", value);  // Uses mutex internally
```

---

## Debugging

### Serial Debug Output

Connect UART2 (PA2=TX, PA15=RX) at 921600 baud:
```bash
minicom -D /dev/ttyUSB0 -b 921600
```

### Stack Usage Monitoring

Built-in stack high water mark checking:
```c
UBaseType_t stack_free = uxTaskGetStackHighWaterMark(NULL);
```

Enable in Monitor task to periodically report usage.

### Runtime Statistics

FreeRTOS runtime stats show CPU usage per task:
```c
vTaskGetRunTimeStats(buffer);
```

Accessible via Telnet command: `stats`

### SEGGER SystemView (Optional)

For advanced profiling, SystemView can be integrated:
1. Add SystemView source to Middleware/
2. Enable trace in FreeRTOSConfig.h
3. Use RTT for real-time trace upload

---

## Testing Plan

### Phase 1: Bare Hardware (No RTOS)
- [x] Clock configuration (80 MHz)
- [x] GPIO initialization
- [x] UART output
- [ ] SPI1 loopback test (SI4463)
- [ ] SPI2 loopback test (W5500 + SRAM)
- [ ] ADC random number generation

### Phase 2: FreeRTOS Bring-up
- [ ] Idle task + tick interrupt
- [ ] Single LED blink task
- [ ] Multiple tasks with different priorities
- [ ] Mutex test (SPI sharing)
- [ ] Queue test (packet passing)
- [ ] Stack overflow detection test

### Phase 3: Driver Porting
- [ ] SI4463 initialization and version read
- [ ] W5500 initialization and PHY link
- [ ] External SRAM detection and read/write
- [ ] TDMA microsecond timer accuracy test

### Phase 4: Protocol Integration
- [ ] TDMA master mode TX
- [ ] TDMA slave mode RX + TA measurement
- [ ] FEC encoding/decoding
- [ ] Packet segmentation/reassembly

### Phase 5: Services
- [ ] DHCP server
- [ ] ARP proxy
- [ ] Telnet CLI
- [ ] SNMP agent
- [ ] Signaling protocol

### Phase 6: Full System Test
- [ ] Master-Slave TDMA link
- [ ] IP packet bridging (Ethernet ↔ Radio)
- [ ] Multi-client TDMA allocation
- [ ] Long-term stability (24h+ test)
- [ ] Temperature recalibration
- [ ] Memory leak detection

---

## Migration Status

### Completed ✅
- Project structure design
- FreeRTOS configuration
- Type definitions
- Architecture documentation

### In Progress 🔄
- HAL initialization code
- Driver porting (SI4463, W5500, SRAM)
- Task implementation
- Build system

### Pending ⏳
- Full driver testing
- Protocol integration
- Service tasks
- Integration testing

---

## Contributing

This is a complete rewrite. Original firmware by F4HDK, FreeRTOS port by llatva.

For questions or issues, refer to the original NPR project:
https://hackaday.io/project/164092-npr-new-packet-radio

---

## License

GNU General Public License v3.0 - see gpl-3.0.txt

**Note:** This firmware is for amateur radio use only. Comply with local regulations for frequency allocation and transmission power.
