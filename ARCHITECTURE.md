# NPR-70 Firmware Architecture

**Version**: 1.8 (June 7, 2026)  
**Status**: Core protocol implementation complete - Ready for hardware testing

This document describes the technical architecture of the NPR-70 modem firmware running on FreeRTOS 11.1.0 LTS. It covers the system design, task architecture, data flow, protocol implementation, and hardware interfaces.

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Platform](#hardware-platform)
3. [FreeRTOS Task Architecture](#freertos-task-architecture)
4. [Data Flow](#data-flow)
5. [Protocol Stack](#protocol-stack)
6. [Memory Architecture](#memory-architecture)
7. [Inter-Task Communication](#inter-task-communication)
8. [Driver Architecture](#driver-architecture)
9. [Error Handling & Monitoring](#error-handling--monitoring)

---

## System Overview

The NPR-70 is a high-speed amateur radio data modem that bridges Ethernet networks over radio links using TDMA (Time Division Multiple Access). The firmware implements a complete protocol stack for bidirectional IPv4 routing between Ethernet and radio interfaces.

### Key Features

- **Bidirectional IPv4 Routing**: Full Ethernet ↔ Radio data path with segmentation and reassembly
- **TDMA Protocol**: 100ms frame duration, 6.84ms slot timing, master/client synchronization
- **FEC Error Correction**: (4,3) forward error correction with CRC validation
- **DHCP Server**: Dynamic IP allocation for radio clients
- **ARP Proxy**: Transparent address resolution for radio-side clients
- **Management Interfaces**: Telnet (TCP port 23) and Serial CLI (USART2, 921600 baud)
- **SNMP Agent**: Network management via UDP port 161

### Design Philosophy

1. **Real-time Operation**: Priority-based preemptive scheduling ensures radio timing requirements are met
2. **Resource Efficiency**: Carefully optimized for 64KB RAM constraint with external SRAM offload
3. **Modularity**: Clear separation of concerns with dedicated tasks for each protocol layer
4. **Reliability**: Watchdog monitoring, error detection, and graceful degradation

---

## Hardware Platform

### Microcontroller: STM32L432KC

- **Core**: ARM Cortex-M4F @ 80 MHz
- **Flash**: 256 KB
- **Internal RAM**: 64 KB
- **FPU**: Hardware floating-point (FPv4-SP-D16)
- **Timers**: TIM2 configured as 1 MHz microsecond counter for TDMA timing

### Peripherals

#### SPI1: Radio Interface
- **Device**: Silicon Labs SI4463 radio transceiver
- **Frequency**: 420-450 MHz (70cm band) or 144-148 MHz (2m band)
- **Modulation**: GMSK with configurable data rates
- **Chip Select**: PA4
- **Interrupt**: PA3 (radio packet events)
- **Mutex**: `xSPI1Mutex` for thread-safe access

#### SPI3: Ethernet & External RAM (Shared Bus)
- **W5500 Ethernet Controller**
  - 10/100 Mbps Ethernet MAC/PHY
  - Hardware TCP/IP offload (8 sockets)
  - Chip Select: PA15
  - Interrupt: PB4
- **23LC1024 External SRAM** (128 KB)
  - RX FIFO buffer storage (2048 bytes)
  - Packet buffer overflow storage
  - Chip Select: PB0
  - Sequential mode for efficient transfers
- **Mutex**: `xSPI3Mutex` for shared bus arbitration

#### UART2: Serial Console
- **Baud Rate**: 921600 baud, 8N1
- **Pins**: PA2 (TX), PA15 (RX)
- **Purpose**: Local CLI access and debugging

### Memory Map

```
Internal SRAM (64 KB):
├── FreeRTOS Heap (16 KB)         - Task stacks, queues, dynamic allocation
├── Task Stacks (~10 KB)          - 9 application tasks + system tasks
├── Global Buffers (~15 KB)       - Static buffers, tables, state
├── BSS/Data (~15 KB)             - Initialized and zero-initialized data
└── Available Headroom (~8 KB)    - Stack growth, temporary buffers

External SRAM (128 KB):
├── RX FIFO Buffer (2 KB)         - Radio receive circular buffer
├── Packet Buffers (~10 KB)       - Ethernet/radio packet staging
└── Reserved (~116 KB)            - Future expansion

Flash (256 KB):
├── Application Code (~70 KB)     - Firmware v1.8 with full routing + FDD downlink
├── HAL/Middleware (~50 KB)       - STM32 HAL, FreeRTOS kernel
├── Configuration Data (~2 KB)    - Settings stored in flash
└── Available (~134 KB)           - Future features, firmware updates
```

---

## FreeRTOS Task Architecture

The firmware runs 9 application tasks with priority-based preemptive scheduling:

### Task Priority Levels

```
Priority 7: Radio Combined Task      [Highest]
Priority 6: TDMA Task
Priority 5: Signaling Task
Priority 4: Ethernet Task
Priority 3: DHCP/ARP Task, SNMP Task
Priority 2: Telnet Task
Priority 1: Serial CLI Task, Monitor Task
Priority 0: IDLE Task                [Lowest]
```

### Task Descriptions

#### 1. Radio Combined Task (Priority 7) — `task_radio_combined.c`

**Purpose**: Combined ISR handling and packet processing for the SI4463 radio transceiver.

**Stack Size**: 240 words (960 bytes)

**Responsibilities**:
- Handle radio interrupts via deferred ISR pattern (xRadioISRQueue)
- Read packets from RX FIFO when available
- FEC decode incoming radio frames
- Reassemble multi-segment IPv4 packets
- Route complete packets based on protocol byte:
  - `0x02` (IPv4): Forward to xEthernetTxQueue → W5500
  - `0x1E` (Signaling): Forward to Signaling_ProcessRxFrame()
  - `0x1F` (TDMA Allocation): Forward to TDMA_ProcessAllocation() (client mode)
  - `0x00` (Null): No-op (keep-alive)
- Transmit packets from xRadioTxQueue
- FEC encode outgoing frames
- Manage per-client reassembly buffers with lazy allocation

**Key Data Structures**:
```c
static uint8_t *reassembly_buffers[RADIO_ADDR_TABLE_SIZE]; // Per-client buffers
static uint16_t size_received[RADIO_ADDR_TABLE_SIZE];      // Reassembly progress
static uint8_t prev_seg_counter[RADIO_ADDR_TABLE_SIZE];    // Continuity checking
static uint8_t curr_pkt_counter[RADIO_ADDR_TABLE_SIZE];    // Packet ID tracking
static uint32_t last_rx_time[RADIO_ADDR_TABLE_SIZE];       // Idle timeout
```

**Critical Timing**: Must respond to radio interrupts within ~100 µs to avoid packet loss.

---

#### 2. TDMA Task (Priority 6) — `task_tdma.c`

**Purpose**: Time Division Multiple Access coordinator for slot synchronization.

**Stack Size**: 160 words (640 bytes)

**Responsibilities**:
- **Master Mode**:
  - Maintain TDMA frame timing (100 ms frame, 6.84 ms slots)
  - Build allocation frames with client slot assignments
  - Broadcast allocation frames at frame start
  - Send null frames during empty slots
  - Track timing advance (TA) for each client
- **Client Mode**:
  - Parse incoming allocation frames from master
  - Synchronize local timing to master's frame counter
  - Calculate transmit slot based on allocation
  - Measure and report timing advance

**Key Constants**:
```c
#define TDMA_FRAME_DURATION_MS  100    // 100 ms TDMA frame
#define TDMA_SLOT_DURATION_MS   6.84   // 6.84 ms per slot
#define TDMA_MAX_SLOTS          14     // Max clients per frame
```

**Timing Source**: TIM2 configured as 1 MHz counter (`HAL_GetMicroseconds()`)

---

#### 3. Signaling Task (Priority 5) — `task_signaling.c`

**Purpose**: Network signaling protocol for client registration and keep-alive.

**Stack Size**: 128 words (512 bytes)

**Responsibilities**:
- Process signaling frames from radio (protocol 0x1E)
- Handle client registration requests
- Maintain client connection state
- Send periodic keep-alive frames
- Update TDMA table with client addresses
- Measure and track timing advance (TA) for synchronization

**Signaling Protocol**:
- Registration: Client announces presence to master
- Keep-alive: Periodic heartbeat to maintain connection
- Disconnect: Clean shutdown notification

---

#### 4. Ethernet Task (Priority 4) — `task_ethernet.c`

**Purpose**: Combined Ethernet RX/TX with IPv4 routing, segmentation, and FDD downlink injection.

**Stack Size**: 200 words (800 bytes)

**Responsibilities**:
- **RX Path**:
  - Poll W5500 sockets for incoming packets
  - Handle raw Ethernet frames (ARP)
  - Route IPv4 packets to radio via segmentation
  - **FDD Downlink** (v1.8): Detect UDP port 6716, inject into RX FIFO
- **TX Path**:
  - Receive reassembled IPv4 packets from xEthernetTxQueue
  - Send to W5500 with proper socket and Ethernet framing
- **Segmentation** (IPv4 → Radio):
  - Split IPv4 packets into 252-byte segments
  - Generate segmenter byte: `pkt_counter[4] | last_flag[1] | reserved[1] | seg_counter[3]`
  - Queue segments to xRadioTxQueue for transmission
  - FEC encoding applied by Radio task
- **FDD Downlink Injection** (v1.8):
  - Detect UDP packets to modem IP on port 6716 (in master FDD mode)
  - Extract UDP payload containing raw radio packet data
  - Inject into RX FIFO via `RX_FIFO_Write()`
  - Notify radio task via xRadioISRQueue for processing
  - Enables frequency-division duplex operation
- **ARP Proxy**:
  - Respond to ARP requests for radio client IPs
  - Learn ARP mappings from incoming packets
  - Maintain ARP table for transparent bridging

**Protocol Byte Assignment**:
- `0x02`: IPv4 data packets
- Other protocols handled by specialized tasks

---

#### 5. DHCP/ARP Task (Priority 3) — `task_dhcp_arp.c`

**Purpose**: DHCP server and ARP proxy for radio clients.

**Stack Size**: 160 words (640 bytes)

**Responsibilities**:
- **DHCP Server** (UDP port 67):
  - Receive DISCOVER/REQUEST from clients via W5500 socket
  - Allocate IPs from configured pool (e.g., 192.168.10.100-199)
  - Send OFFER/ACK responses
  - Maintain lease table with timers
  - Support broadcast (255.255.255.255)
- **ARP Proxy**:
  - Maintain ARP table for radio clients
  - Proxy ARP requests/replies between Ethernet and radio
  - Age out stale entries

**DHCP Transaction Flow**:
```
Client                  Master (DHCP Server)
  |                            |
  |-------- DISCOVER --------->|
  |                            |  Allocate IP from pool
  |<-------- OFFER ------------|
  |                            |
  |-------- REQUEST ---------->|
  |                            |  Commit lease
  |<-------- ACK --------------|
  |                            |
```

---

#### 6. SNMP Task (Priority 3) — `task_snmp.c`

**Purpose**: SNMP agent for network management.

**Stack Size**: 160 words (640 bytes)

**Responsibilities**:
- Listen on UDP port 161 via W5500 socket
- Parse SNMP GET/SET requests
- Respond with NPR-70 MIB variables
- Provide monitoring data: status, statistics, configuration

**MIB Variables** (example):
- System info: firmware version, uptime
- Radio status: frequency, modulation, TX power
- Network stats: packet counts, error rates
- TDMA info: slot allocation, timing advance

---

#### 7. Telnet Task (Priority 2) — `task_telnet.c`

**Purpose**: Remote CLI via TCP port 23.

**Stack Size**: 160 words (640 bytes)

**Responsibilities**:
- Accept TCP connections on W5500 socket 1
- Process CLI commands using shared `cli_commands.c` library
- Send responses back to client
- Handle disconnect gracefully

**Command Library**: Shares code with Serial CLI (`cli_commands.c/h`)

---

#### 8. Serial CLI Task (Priority 1) — `task_serial_cli.c`

**Purpose**: Local CLI via UART2 (921600 baud).

**Stack Size**: 512 words (2048 bytes)

**Responsibilities**:
- Read commands from UART2 (PA2/PA15)
- Process CLI commands using shared `cli_commands.c` library
- Echo characters and send responses
- Provide identical command set to Telnet

**Shared Command Library** (`cli_commands.c`):
```c
CLI_Status CLI_ProcessCommand(const char *cmd_line, 
                              void (*output_fn)(const char *str));
```

**Available Commands**:
- `help`, `version`, `status`, `who`
- `show config`, `show tasks`, `show memory`, `show dhcp`
- `radio on/off`, `save`, `reboot`, `reset_to_default`
- `set <param> <value>` (network_id, frequency, modulation, is_master, callsign)

---

#### 9. Monitor Task (Priority 1) — `task_monitor.c`

**Purpose**: System health monitoring and maintenance.

**Stack Size**: 128 words (512 bytes)

**Responsibilities**:
- **Temperature Recalibration** (every 30 seconds):
  - Call `SI4463_CheckTemperatureCalibration()`
  - Log recalibration events when temperature drifts >10°C
  - Ensures radio frequency accuracy over temperature changes
- **Stack Usage Monitoring**:
  - Check stack high-water marks for all tasks
  - Log warnings if any task approaches stack overflow
- **Statistics Collection**:
  - Track check count, recalibration count
  - Monitor minimum stack free for each task

**Key Function**:
```c
void SI4463_CheckTemperatureCalibration(SI4463_Handle *hsi4463);
// Returns: Triggers recalibration if temp drift > SI4463_TEMP_RECAL_THRESHOLD (10°C)
```

---

## Data Flow

### TX Path: Ethernet → Radio

```
W5500 Ethernet Controller
    ↓
task_ethernet.c (RX polling)
    ↓
RouteIPv4ToRadio() - Identifies radio-destined packets
    ↓
Segmentation (252 bytes per segment)
    • Generate segmenter byte: pkt_counter[4] | last_flag[1] | seg_counter[3]
    • Add protocol byte 0x02 (IPv4)
    ↓
xRadioTxQueue (segments queued)
    ↓
task_radio_combined.c (TX processing)
    ↓
FEC_Encode() - (4,3) forward error correction
    ↓
SI4463_WriteTxFifo() - Load into radio TX FIFO
    ↓
SI4463_PrepareTX() - Trigger transmission
    ↓
Radio RF Output → Over the air
```

**Segmentation Details**:
- **Max Segment Size**: 252 bytes (payload) + 1 byte (segmenter) + 1 byte (protocol) = 254 bytes
- **Segmenter Byte**: `[pkt_counter:4] [last_flag:1] [reserved:1] [seg_counter:3]`
  - `pkt_counter`: Packet ID (0-15), wraps around
  - `last_flag`: Set on final segment of packet
  - `seg_counter`: Segment index within packet (0-7)
- **FEC Encoding**: 3 data blocks + 1 XOR parity block + 4 CRC bytes
- **Total Frame Size**: ~340 bytes after FEC encoding

---

### RX Path: Radio → IPv4

```
Radio RF Input → SI4463 RX FIFO
    ↓
SI4463 Interrupt (PA3)
    ↓
xRadioISRQueue (deferred ISR)
    ↓
task_radio_combined.c (RX processing)
    ↓
SI4463_ReadRxFifo() - Extract packet from FIFO
    ↓
FEC_Decode() - Validate and decode
    • Check CRC, correct errors
    • Calculate BER (Bit Error Rate)
    ↓
Extract protocol byte and client ID
    ↓
Client ID Filtering
    • Master: Accept uplink from registered clients + discovery (0x7E)
    • Client: Accept downlink to my_radio_client_ID or broadcast (0x7F)
    ↓
Protocol Switch:
    ├─ 0x02 (IPv4) → Segment Reassembly
    │       ↓
    │   Parse segmenter byte
    │   Continuity check (seg_counter == prev + 1)
    │   Append to reassembly buffer
    │   If last_flag set:
    │       ↓
    │   ProcessIPv4Packet() → xEthernetTxQueue
    │       ↓
    │   task_ethernet.c → W5500_WritePacket()
    │       ↓
    │   Ethernet Output
    │
    ├─ 0x1E (Signaling) → Signaling_ProcessRxFrame()
    │
    ├─ 0x1F (TDMA Allocation) → TDMA_ProcessAllocation() (client mode)
    │
    └─ 0x00 (Null) → No-op (keep-alive)
```

**Reassembly Details**:
- **Per-Client Buffers**: Separate 1600-byte buffer for each client (lazy allocation)
- **Continuity Checking**: 
  - `seg_counter` must increment by 1 for each segment
  - `pkt_counter` must match across all segments of same packet
  - Mismatch triggers buffer reset and error counter increment
- **Idle Timeout**: Buffers freed after 60 seconds of inactivity to prevent heap fragmentation
- **Buffer Offset**: IPv4 data stored at offset +14 to reserve space for Ethernet header

---

## Protocol Stack

### Layer 1: Physical (SI4463 Radio)

**Modulation**: GMSK (Gaussian Minimum Shift Keying)  
**Frequency Range**: 420-450 MHz (70cm) or 144-148 MHz (2m)  
**Data Rates**: Variable (typically 100-200 kbps)  
**TX Power**: Configurable up to 100 mW (20 dBm)

**Driver**: `Application/Radio/si4463_driver.c`

**Key Functions**:
```c
SI4463_Status SI4463_Init(SI4463_Handle *hsi4463);
SI4463_Status SI4463_SetFrequency(SI4463_Handle *hsi4463, uint32_t freq_hz);
SI4463_Status SI4463_PrepareTX(SI4463_Handle *hsi4463);
SI4463_Status SI4463_PrepareRX(SI4463_Handle *hsi4463);
SI4463_Status SI4463_ReadRxFifo(SI4463_Handle *hsi4463, uint8_t *buffer, uint16_t *length);
SI4463_Status SI4463_WriteTxFifo(SI4463_Handle *hsi4463, const uint8_t *data, uint16_t length);
void SI4463_CheckTemperatureCalibration(SI4463_Handle *hsi4463);
```

---

### Layer 2: Data Link (FEC + TDMA)

#### FEC (Forward Error Correction)

**Algorithm**: (4,3) block code with XOR parity  
**Implementation**: `Application/Common/fec_codec.c`

**Encoding**:
```
Input: 3 data blocks (252 bytes each)
Output: 3 data blocks + 1 parity block (XOR of all data) + 4 CRC bytes
Total: 4 blocks × 64 bytes/block + 4 CRC = 260 bytes encoded
```

**Decoding**:
```c
int FEC_Decode(uint8_t *data_out, int size_in, uint32_t *micro_BER);
// Returns: Decoded size (0 on error)
// micro_BER: Bit error rate × 10^6 for monitoring
```

**Error Correction**: Single block errors correctable using parity

---

#### TDMA (Time Division Multiple Access)

**Frame Structure**:
```
Frame Duration: 100 ms
├── Slot 0:   Master broadcast (allocation frame or signaling)
├── Slot 1:   Client 1 uplink (if allocated)
├── Slot 2:   Client 2 uplink (if allocated)
├── ...
├── Slot 13:  Client 13 uplink (if allocated)
└── Slot 14:  Master downlink / null frame

Slot Duration: 6.84 ms (100 ms / 14.63 theoretical slots ≈ 14 usable)
```

**Allocation Frame** (Protocol 0x1F):
```
Byte 0:       Frame counter (0-255, wraps)
Byte 1-N:     Client allocation entries
Each entry:   [client_addr:1] [slot_number:1] [TA_correction:2]
```

**Timing Advance (TA)**:
- Measured by master based on received packet timestamps
- Correction sent to clients in allocation frame
- Compensates for propagation delay and clock drift
- Resolution: ~1 µs (using TIM2 microsecond counter)

---

### Layer 3: Network (IPv4 Routing)

**Protocol Byte**: Identifies packet type in radio frames
- `0x00`: Null (keep-alive, no payload)
- `0x02`: IPv4 data packet
- `0x1E`: Signaling (registration, keep-alive, disconnect)
- `0x1F`: TDMA allocation frame (master → clients)

**Client ID Byte**: Identifies sender/receiver
- `0x00-0x3F`: Normal client addresses (master assigns)
- `0x7E`: Discovery (unregistered client seeking master)
- `0x7F`: Broadcast (master → all clients)

**Routing Logic**:

**Master Mode**:
- RX: Accept uplink from registered clients + discovery (0x7E)
- TX: Send downlink to specific client or broadcast (0x7F)
- IPv4 forwarding: Route Ethernet packets to radio based on ARP table

**Client Mode**:
- RX: Accept downlink to my_radio_client_ID or broadcast (0x7F)
- TX: Send uplink with my_radio_client_ID as source
- IPv4 forwarding: Route all Ethernet traffic to master (default gateway)

---

### Layer 4: Transport (DHCP, ARP, Telnet)

#### DHCP Server

**Implementation**: `Application/Services/dhcp_server.c`

**IP Pool Configuration**:
```c
#define DHCP_POOL_START  192.168.10.100  // First assignable IP
#define DHCP_POOL_END    192.168.10.199  // Last assignable IP
#define DHCP_LEASE_TIME  86400           // 24 hours in seconds
```

**Lease Table**:
```c
typedef struct {
    uint8_t client_addr;        // Radio client ID
    uint32_t ip_address;        // Assigned IPv4 address
    uint32_t lease_expiry;      // Expiry timestamp (FreeRTOS ticks)
    uint8_t state;              // OFFERED, LEASED, EXPIRED
} DHCP_Lease;
```

---

#### ARP Proxy

**Implementation**: `Application/Services/arp_proxy.c`

**Purpose**: Transparently bridge Ethernet ARP with radio clients

**ARP Table**:
```c
typedef struct {
    uint32_t ip_address;        // IPv4 address
    uint8_t mac_address[6];     // Ethernet MAC (for Ethernet side)
    uint8_t radio_client_addr;  // Radio client ID (for radio side)
    uint32_t last_seen;         // Age-out timestamp
} ARP_Entry;
```

**Operations**:
- **ARP Request** from Ethernet for radio client IP:
  - Proxy responds with own MAC address
  - Routes subsequent packets to radio
- **ARP Reply** from Ethernet:
  - Learn mapping in ARP table
  - Forward to radio if needed

---

## Memory Architecture

### Heap Management

**Allocator**: FreeRTOS heap_4.c (first-fit with coalescing)  
**Heap Size**: 16,384 bytes (16 KB)  
**Configuration**: `FreeRTOSConfig.h` → `configTOTAL_HEAP_SIZE`

**Heap Usage**:
```
FreeRTOS Overhead:
├── Task Control Blocks (TCBs): 9 tasks × ~100 bytes = 900 bytes
├── Task Stacks: (See task descriptions above) ≈ 10 KB
├── Queues: 4 queues × ~200 bytes each = 800 bytes
├── Mutexes: 3 mutexes × ~100 bytes each = 300 bytes
└── Available for dynamic allocation: ~4 KB

Dynamic Allocations (on demand):
├── Reassembly buffers: 1600 bytes × 4 clients (lazy) = 6400 bytes max
├── CLI buffers: ~512 bytes (telnet + serial)
└── Temporary buffers: ~1 KB
```

**Critical Constraint**: Total heap usage must stay under 16 KB to avoid malloc failures.

---

### Stack Sizing

Each task has a statically allocated stack (word-sized, 4 bytes/word):

| Task             | Stack Words | Stack Bytes | Rationale                           |
|------------------|-------------|-------------|-------------------------------------|
| Radio Combined   | 240         | 960         | Large for FEC buffers + recursion   |
| TDMA             | 160         | 640         | Timing calculations, allocation     |
| Signaling        | 128         | 512         | Frame processing                    |
| Ethernet         | 200         | 800         | Segmentation logic                  |
| DHCP/ARP         | 160         | 640         | DHCP transaction handling           |
| SNMP             | 160         | 640         | SNMP PDU parsing                    |
| Telnet           | 160         | 640         | TCP socket + CLI processing         |
| Serial CLI       | 512         | 2048        | UART buffers + CLI processing       |
| Monitor          | 128         | 512         | Simple periodic checks              |

**Total**: ~7.3 KB for application task stacks

**Stack Overflow Detection**: FreeRTOS stack overflow checking enabled (`configCHECK_FOR_STACK_OVERFLOW 2`)

---

### External SRAM Usage

**Chip**: Microchip 23LC1024 (128 KB SPI SRAM)  
**Interface**: SPI3 (shared with W5500)  
**Mode**: Sequential read/write for efficiency

**Allocations**:
```c
// Defined in ext_sram_driver.c
#define EXT_SRAM_RX_FIFO_SIZE    2048   // Radio RX circular buffer
#define EXT_SRAM_PACKET_BUF_SIZE 1600   // Ethernet packet staging

// Memory map:
// 0x0000-0x07FF: RX FIFO (2 KB)
// 0x0800-0x0E3F: Packet buffer #1 (1600 bytes)
// 0x0E40-0x147F: Packet buffer #2 (1600 bytes)
// ...
// 0x1F000-0x1FFFF: Reserved for future use
```

**Boot-Time Validation**:
```c
SRAM_Status SRAM_Init(void);
// Performs read/write test pattern
// Returns: SRAM_OK or SRAM_ERROR
// On error: Firmware halts with error message on serial console
```

---

## Inter-Task Communication

### Queues

**Purpose**: Thread-safe message passing between tasks

**Defined Queues** (`app_common.h`):

```c
extern QueueHandle_t xRadioISRQueue;      // Radio ISR → Radio task
extern QueueHandle_t xRadioTxQueue;       // Ethernet → Radio (segments)
extern QueueHandle_t xEthernetRxQueue;    // W5500 → Ethernet (not used in v1.7)
extern QueueHandle_t xEthernetTxQueue;    // Radio → Ethernet (reassembled)
```

**Queue Depths**:
- `xRadioISRQueue`: 4 items (deferred ISR notifications)
- `xRadioTxQueue`: 4 items (radio TX segments, 384 bytes each)
- `xEthernetTxQueue`: 2 items (Ethernet TX packets, 1600 bytes each)

**Item Structures**:
```c
// Radio TX Queue Item
typedef struct {
    uint8_t client_addr;
    uint8_t protocol;
    uint16_t length;
    uint8_t data[384];  // Max segment size
} RadioPacket_t;

// Ethernet TX Queue Item
typedef struct {
    uint8_t socket;
    uint16_t length;
    uint8_t data[1600];  // Full Ethernet MTU
} EthernetPacket_t;
```

---

### Mutexes

**Purpose**: Protect shared resources from concurrent access

**Defined Mutexes** (`app_common.h`):

```c
extern SemaphoreHandle_t xSPI1Mutex;   // SI4463 radio SPI bus
extern SemaphoreHandle_t xSPI3Mutex;   // W5500 + SRAM shared SPI bus
extern SemaphoreHandle_t xConfigMutex; // Configuration flash access
```

**Usage Pattern**:
```c
if (xSemaphoreTake(xSPI3Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // Critical section: SPI3 operations
    W5500_ReadPacket(...);
    xSemaphoreGive(xSPI3Mutex);
} else {
    // Timeout: log error
}
```

**Critical Timing**: SPI3 mutex contention between W5500 (high-frequency polling) and SRAM (RX FIFO access) can cause delays. Minimize SPI3 critical section duration.

---

## Driver Architecture

### SI4463 Radio Driver

**File**: `Application/Radio/si4463_driver.c`

**Initialization Sequence**:
1. Assert SHDN (shutdown) pin, wait 10 ms
2. Deassert SHDN, wait for POR (power-on reset)
3. Apply RF configuration patch (register writes)
4. Set frequency, modulation, TX power
5. Enable interrupts for packet events
6. Enter RX mode (ready to receive)

**State Machine**:
```
IDLE → (PrepareTX) → TX → (packet sent) → RX
     ← (PrepareRX) ← TX_COMPLETE ← TX
```

**Interrupt Handling**:
- **Hardware IRQ** (PA3) → Set flag
- **Radio Task** polls flag → Read interrupt status register
- **Packet RX**: Read FIFO, push to xRadioISRQueue
- **Packet TX**: Transition back to RX mode

---

### W5500 Ethernet Driver

**File**: `Application/Ethernet/w5500_driver.c`

**Socket Configuration**:

| Socket | Protocol | Port | Purpose                          |
|--------|----------|------|----------------------------------|
| 0      | RAW      | -    | ARP (Ethernet Type 0x0806)       |
| 1      | TCP      | 23   | Telnet console                   |
| 3      | UDP      | 67   | DHCP server                      |
| 5      | UDP      | 161  | SNMP agent                       |

**Polling Loop** (in Ethernet Task):
```c
while (1) {
    for (int socket = 0; socket < 8; socket++) {
        uint16_t rx_size = W5500_GetRxSize(socket);
        if (rx_size > 0) {
            W5500_ReadPacket(socket, buffer, rx_size);
            // Route to appropriate handler
        }
    }
    vTaskDelay(pdMS_TO_TICKS(5));  // 5 ms polling interval
}
```

**SPI Protocol**: W5500 uses frame-based SPI with control byte encoding socket/register.

---

### External SRAM Driver

**File**: `Application/Memory/ext_sram_driver.c`

**Initialization**:
```c
SRAM_Status SRAM_Init(void) {
    SRAM_WriteByte(0x0000, 0xAA);
    uint8_t test = SRAM_ReadByte(0x0000);
    if (test == 0xAA) {
        SRAM_WriteByte(0x0000, 0x55);
        test = SRAM_ReadByte(0x0000);
        if (test == 0x55) return SRAM_OK;
    }
    return SRAM_ERROR;  // Test failed
}
```

**Sequential Mode**: Efficient for bulk transfers (RX FIFO circular buffer)

**Functions**:
```c
SRAM_Status SRAM_Read(uint32_t address, uint8_t *buffer, uint16_t length);
SRAM_Status SRAM_Write(uint32_t address, const uint8_t *buffer, uint16_t length);
```

---

## Error Handling & Monitoring

### Watchdog (Planned)

**Purpose**: Reset system if any task hangs or deadlocks

**Implementation Status**: ⚠️ Not yet implemented in v1.7

**Planned Design**:
- Independent watchdog (IWDG) with ~10 second timeout
- Each task kicks watchdog via shared flag
- Monitor task verifies all flags set before kick
- Missing flag → log error, allow reset

---

### Stack Overflow Detection

**Mechanism**: FreeRTOS stack overflow hook (`configCHECK_FOR_STACK_OVERFLOW 2`)

**Hook Function** (`main.c`):
```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    printf("STACK OVERFLOW in task: %s\n", pcTaskName);
    // System halt (LED blink pattern, serial output)
    while (1);
}
```

**Prevention**: Monitor task checks `uxTaskGetStackHighWaterMark()` for each task

---

### Temperature Recalibration

**Purpose**: Maintain radio frequency accuracy over temperature changes

**Implementation**: Monitor task (every 30 seconds)

**Function**:
```c
void SI4463_CheckTemperatureCalibration(SI4463_Handle *hsi4463);
// Reads current temperature from SI4463 internal sensor
// Compares to last calibration temperature
// If drift > 10°C: Trigger recalibration (re-apply frequency settings)
```

**Threshold**: `SI4463_TEMP_RECAL_THRESHOLD = 10°C`

---

### FEC Error Tracking

**Metric**: Bit Error Rate (BER)

**Calculation** (in `FEC_Decode()`):
```c
uint32_t errors_corrected = /* count of corrected bit errors */;
uint32_t total_bits = size_in * 8;
*micro_BER = (errors_corrected * 1000000) / total_bits;
// micro_BER: Errors per million bits
```

**Monitoring**: Radio task logs BER for diagnostics

---

## Build System

### Toolchain

- **Compiler**: arm-none-eabi-gcc 13.2.1
- **Linker**: arm-none-eabi-ld (via gcc)
- **Build**: GNU Make
- **Debug**: arm-none-eabi-gdb + OpenOCD

### Makefile Targets

```bash
make          # Build firmware
make clean    # Clean build artifacts
make flash    # Flash to target (requires st-flash)
make size     # Display memory usage
```

### Compiler Flags

```makefile
CFLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS += -Og -g3 -Wall -Wextra
CFLAGS += --specs=nano.specs -ffunction-sections -fdata-sections

LDFLAGS = -T STM32L432KC.ld --specs=nano.specs 
LDFLAGS += -Wl,--gc-sections -Wl,-Map=build/NPR70_FreeRTOS.map
```

**Optimization**: `-Og` (debug with optimization) balances debuggability with code size

---

## Configuration

### FreeRTOS Configuration

**File**: `Core/Inc/FreeRTOSConfig.h`

**Key Settings**:
```c
#define configUSE_PREEMPTION              1      // Preemptive scheduling
#define configCPU_CLOCK_HZ                80000000  // 80 MHz
#define configTICK_RATE_HZ                1000   // 1 ms tick
#define configTOTAL_HEAP_SIZE             16384  // 16 KB heap
#define configMINIMAL_STACK_SIZE          128    // 128 words (512 bytes)
#define configMAX_PRIORITIES              8      // Priority 0-7
#define configCHECK_FOR_STACK_OVERFLOW    2      // Enable overflow detection
```

---

### Radio Configuration

**File**: `Application/Radio/si4463_config.h`

**Default Settings**:
```c
#define DEFAULT_FREQUENCY_HZ      437000000  // 437.000 MHz
#define DEFAULT_NETWORK_ID        0          // Network ID 0-15
#define DEFAULT_MODULATION        22         // Modulation index
#define DEFAULT_TX_POWER          20         // 20 dBm (100 mW)
```

---

## Future Enhancements

1. **Power Management**: Sleep modes for battery operation
2. **Firmware Update**: Bootloader for OTA updates
3. **Extended Diagnostics**: Ring buffer logging, statistics collection
4. **Performance Tuning**: Throughput optimization, latency reduction
5. **Watchdog**: Independent watchdog implementation for reliability

---

## References

- **FreeRTOS Documentation**: https://www.freertos.org/
- **STM32L4 Reference Manual**: RM0394 (STMicroelectronics)
- **SI4463 Datasheet**: Silicon Labs
- **W5500 Datasheet**: WIZnet
- **Original NPR-70 Project**: https://hackaday.io/project/164092-npr-new-packet-radio

---

**Document Version**: 1.0  
**Last Updated**: June 7, 2026  
**Author**: OH3HZB (FreeRTOS port architecture)
