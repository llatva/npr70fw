# NPR-70 Firmware Memory Consumption Analysis

## Executive Summary

This document provides a comprehensive analysis of memory (RAM) consumption in the NPR-70 modem firmware. The system is based on the STM32L432KC microcontroller with **64KB of RAM** (with ~392 bytes reserved for system use, leaving approximately **63.6KB available**). This is a **RAM-constrained system**, and careful memory management is critical for reliable operation.

The firmware supports two operational modes:
1. **No external SRAM** - All buffers stored in internal MCU RAM
2. **External SRAM installed** - Large TX buffers offloaded to external SPI SRAM chip

## Hardware Platform

- **Microcontroller**: STM32L432KC (Cortex-M4F)
- **Internal SRAM**: 64KB (0x20000000 - 0x20010000)
  - Available RAM: ~63.6KB (after system reserved area at 0x20000000-0x20000188)
- **Flash**: 256KB
- **Optional External SRAM**: SPI-connected external SRAM chip (23LC1024 or similar, typically 128KB)

## Memory Configuration Detection

The firmware detects external SRAM availability at boot:
- Function: `ext_SRAM_detect()` in `source/L1L2_radio.cpp` (line 714)
- Detection method: Writes test pattern to external SRAM and reads back to verify
- Result stored in: `is_SRAM_ext` global variable (1 = installed, 0 = not installed)

## Major Buffer Allocations

### 1. Core Communication Buffers (Always Allocated)

#### RX FIFO Buffer (Radio Reception)
**Location**: `source/global_variables.cpp` line 41
```cpp
unsigned char RX_FIFO_data[0x2000]; // 8kB
```
- **Size**: 8,192 bytes (8KB)
- **Purpose**: Circular buffer for received radio packets
- **Configuration independent**: Always allocated regardless of external SRAM
- **Critical**: This is the primary receiver buffer from the SI4463 radio chip

### 2. TX Buffers - Configuration Dependent

#### Internal TX Buffer (Always Allocated)
**Location**: `source/global_variables.cpp` line 54
```cpp
unsigned char TX_buff_intern_FIFOdata[128][128];
```
- **Size**: 16,384 bytes (16KB)
- **Purpose**: Internal transmit packet FIFO
- **Structure**: 128 slots of 128 bytes each
- **Usage**:
  - **Without external SRAM**: Primary TX buffer (heavily used)
  - **With external SRAM**: Used as intermediate/fast-access buffer

#### External TX Buffer Size Tracking (Always Allocated)
**Location**: `source/global_variables.cpp` line 59
```cpp
unsigned char TX_buff_ext_sizes[1024];
```
- **Size**: 1,024 bytes (1KB)
- **Purpose**: Track packet sizes in external SRAM buffer
- **Usage**: Only meaningful when external SRAM is present, but always allocated

#### TDMA Internal Data Buffer (Always Allocated)
**Location**: `source/global_variables.cpp` line 64
```cpp
unsigned char TX_TDMA_intern_data[384];
```
- **Size**: 384 bytes
- **Purpose**: TDMA frame construction buffer

### 3. TDMA Signaling Buffer
**Location**: `source/global_variables.cpp` (referenced in global_variables.h line 90)
```cpp
unsigned char TX_signaling_TDMA_frame[300];
```
- **Size**: 300 bytes
- **Purpose**: TDMA signaling frame construction

### 4. Static Function Buffers

#### Radio Layer (L1L2_radio.cpp)
```cpp
static unsigned char trash[130];                          // 130 bytes
static unsigned char data_RX[360];                        // 360 bytes
static unsigned char rframe_TX[384];                      // 384 bytes
static unsigned char ethernet_buffer[7][1600];            // 11,200 bytes (7 clients × 1600 bytes)
static int size_received[7];                              // 28 bytes
static unsigned char prev_seg_counter[7];                 // 7 bytes
static unsigned char curr_pkt_counter[7];                 // 7 bytes
```
- **Total**: ~12,116 bytes (11.8KB)
- **Note**: `radio_addr_table_size` is defined as 7

#### Virtual Channel (Virt_Chan.cpp)
```cpp
static unsigned char RX_data[1600];                       // 1,600 bytes
static unsigned char IP_addr_1[6];                        // 6 bytes
static unsigned char eth_peer[8];                         // 8 bytes
```
- **Total**: ~1,614 bytes (1.6KB)

#### SI4463 Radio Driver (SI4463.cpp)
```cpp
static unsigned char SI_trash[150];                       // 150 bytes
static unsigned char TX_temp_rframe[384];                 // 384 bytes
```
- **Total**: 534 bytes

#### DHCP/ARP Tables (DHCP_ARP.cpp)
```cpp
static unsigned char DHCP_ARP_MAC[32][6];                 // 192 bytes
static unsigned long int DHCP_ARP_IP[32];                 // 128 bytes
static unsigned int DHCP_ARP_date[32];                    // 128 bytes
static unsigned char DHCP_answer[400];                    // 400 bytes
```
- **Total**: 848 bytes
- **Note**: `DHCP_ARP_tab_size` is 32

#### TDMA Management (TDMA.cpp)
```cpp
static unsigned char TDMA_table_uplink_st[7];             // 7 bytes
static int TDMA_table_uplink_usage[7];                    // 28 bytes
static int TDMA_table_is_fast[7];                         // 28 bytes
static unsigned int TDMA_table_RX_time[7];                // 28 bytes
static unsigned char TDMA_table_up2date[7];               // 7 bytes
static unsigned char TDMA_table_slots[7];                 // 7 bytes
static unsigned int TDMA_table_offset[7];                 // 28 bytes
```
- **Total**: 133 bytes

### 5. External SRAM Management Structures (Only when EXT_SRAM_USAGE is defined)

**Location**: `source/ext_SRAM2.cpp` (lines 24-32)
```cpp
static unsigned char trash[350];                          // 350 bytes
static unsigned short int extSRAM_FIFOs[8][94];          // 1,504 bytes (8 FIFOs × 94 pointers × 2 bytes)
static unsigned char extSRAM_filling[374];                // 374 bytes
static int extSRAM_total_filling;                         // 4 bytes
static unsigned char extSRAM_pkt_timer[374];              // 374 bytes
static unsigned short int extSRAM_pkt_size[374];          // 748 bytes (374 × 2 bytes)
static unsigned char extSRAM_FIFO_index_read[8];          // 8 bytes
static unsigned char extSRAM_FIFO_index_write[8];         // 8 bytes
static unsigned char extSRAM_FIFO_filling[8];             // 8 bytes
```
- **Total**: 3,378 bytes (3.3KB)
- **Note**: Currently `EXT_SRAM_USAGE` is **commented out** in global_variables.h (line 23), so these structures are **NOT compiled** in the current build

### 6. Configuration and State Variables

#### Network Configuration
```cpp
LAN_conf_T LAN_conf_saved;                                // ~32 bytes
LAN_conf_T LAN_conf_applied;                              // ~32 bytes
unsigned char CONF_modem_MAC[6];                          // 6 bytes
```

#### Radio Address Table
```cpp
char CONF_radio_my_callsign[16];                          // 16 bytes
char CONF_radio_master_callsign[16];                      // 16 bytes
unsigned long int CONF_radio_addr_table_IP_begin[7];      // 28 bytes
unsigned long int CONF_radio_addr_table_IP_size[7];       // 28 bytes
char CONF_radio_addr_table_callsign[7][16];               // 112 bytes
char CONF_radio_addr_table_status[7];                     // 7 bytes
unsigned int CONF_radio_addr_table_date[7];               // 28 bytes
long int TDMA_table_TA[7];                                // 28 bytes
unsigned short int G_radio_addr_table_RSSI[7];            // 14 bytes
unsigned short int G_radio_addr_table_BER[7];             // 14 bytes
```
- **Total**: ~343 bytes

#### Parity Lookup Tables
```cpp
unsigned char parity_bit_elab[128];                       // 128 bytes
unsigned char parity_bit_check[256];                      // 256 bytes
```
- **Total**: 384 bytes

### 7. HMI/Telnet Buffers

```cpp
char HMI_out_str[120];                                    // 120 bytes
static char current_rx_line[100];                         // 100 bytes (in HMI_telnet.cpp)
```
- **Total**: 220 bytes

### 8. mbed-OS System Buffers

From `mbed_config.h`:
```cpp
MBED_CONF_EVENTS_SHARED_STACKSIZE         1024          // 1KB
MBED_CONF_EVENTS_SHARED_EVENTSIZE         256           // 256 bytes
MBED_CONF_EVENTS_SHARED_HIGHPRIO_STACKSIZE 1024         // 1KB
MBED_CONF_EVENTS_SHARED_HIGHPRIO_EVENTSIZE 256          // 256 bytes
MBED_CONF_DRIVERS_UART_SERIAL_RXBUF_SIZE  256           // 256 bytes
MBED_CONF_DRIVERS_UART_SERIAL_TXBUF_SIZE  256           // 256 bytes
```
- **Total**: ~3KB (estimated, plus RTOS overhead)

## Memory Usage Summary

### Scenario 1: No External SRAM Available

| Component | Size | Notes |
|-----------|------|-------|
| **RX FIFO Buffer** | 8,192 bytes | 8KB - Radio reception |
| **TX Internal Buffer** | 16,384 bytes | 16KB - Primary TX buffer |
| **TX Size Tracking** | 1,024 bytes | 1KB |
| **TDMA Internal Data** | 384 bytes | |
| **TDMA Signaling Frame** | 300 bytes | |
| **Radio Layer Static Buffers** | 12,116 bytes | 11.8KB - Including ethernet_buffer[7][1600] |
| **Virtual Channel Buffers** | 1,614 bytes | 1.6KB |
| **SI4463 Driver Buffers** | 534 bytes | |
| **DHCP/ARP Tables** | 848 bytes | |
| **TDMA Management** | 133 bytes | |
| **Configuration/State** | 343 bytes | |
| **Parity Tables** | 384 bytes | |
| **HMI Buffers** | 220 bytes | |
| **mbed-OS System** | ~3,000 bytes | 3KB (estimated) |
| **Stack/Heap** | Variable | Remaining RAM |
| **TOTAL STATIC** | **~45,476 bytes** | **~44.4KB** |
| **Available for Stack/Heap** | **~18KB** | Out of 63.6KB available |

**Memory Pressure**: HIGH
- 44.4KB of static allocations leaves only ~18KB for stack and heap
- The large ethernet_buffer[7][1600] = 11.2KB is a significant contributor
- Limited headroom for deep call stacks or dynamic allocations

### Scenario 2: External SRAM Installed

When external SRAM is detected:
- TX buffer writes are redirected to external SPI SRAM (via `TX_ext_FIFO_write()`)
- External SRAM provides 128KB of additional buffer space (23LC1024 chip)
- Internal `TX_buff_intern_FIFOdata[128][128]` still allocated but less heavily used
- Acts as a fast intermediate buffer between Ethernet and external SRAM

| Component | Size | Notes |
|-----------|------|-------|
| **Same as "No External SRAM"** | ~45,476 bytes | All internal buffers remain |
| **External SRAM Management** | 0 bytes | Currently disabled (EXT_SRAM_USAGE not defined) |
| **External SPI SRAM** | 131,072 bytes | 128KB - Off-chip, accessed via SPI |

**Memory Pressure**: MODERATE
- Same internal RAM usage (~44.4KB static)
- TX packets stored in external SRAM instead of internal RAM
- Reduces pressure on internal TX FIFO but doesn't free internal RAM
- External SRAM accessed via SPI (slower but much larger capacity)
- Trade-off: Latency vs capacity

### Key Differences Between Configurations

#### Without External SRAM
**Advantages**:
- Faster access to all buffers (direct RAM access)
- No SPI communication overhead
- Simpler code path

**Disadvantages**:
- Very limited TX buffering capacity (16KB internal FIFO)
- Higher memory pressure
- Risk of buffer overruns during traffic bursts
- Only ~18KB available for stack/heap/dynamic allocations

#### With External SRAM
**Advantages**:
- Massive TX buffer capacity (128KB external + 16KB internal)
- Reduced risk of buffer overruns
- Better handling of traffic bursts
- Same stack/heap availability (~18KB)

**Disadvantages**:
- SPI access latency to external SRAM
- More complex buffer management
- External hardware dependency
- Slightly higher power consumption

## Buffer Management Logic

### TX Buffer Selection
**Function**: `TX_FIFO_write_global()` in `source/L1L2_radio.cpp` (line 476)

```cpp
void TX_FIFO_write_global(unsigned char* data, int size) {
    if (is_SRAM_ext == 1) {
        TX_ext_FIFO_write(data, size);  // Use external SRAM
    } else {
        TX_intern_FIFO_write(data, size); // Use internal RAM
    }
}
```

### External SRAM Write
**Function**: `TX_ext_FIFO_write()` in `source/L1L2_radio.cpp` (line 518)
- Writes packets to external SRAM via SPI
- Supports packets up to 384 bytes (split into 128-byte chunks)
- Tracks packet sizes in `TX_buff_ext_sizes[1024]` array
- Total capacity: 1024 slots × 128 bytes = 128KB

### Internal FIFO Write
**Function**: `TX_intern_FIFO_write()` in `source/L1L2_radio.cpp` (line 484)
- Writes packets to internal RAM buffer
- 128 slots of 128 bytes = 16KB capacity
- Circular buffer with write/read pointers

## External SRAM Details

### Current State
The `EXT_SRAM_USAGE` macro is **commented out** in `global_variables.h` line 23:
```cpp
//#define EXT_SRAM_USAGE
```

This means:
- External SRAM management structures (3.3KB) are **NOT compiled** into the firmware
- The advanced FIFO management code in `ext_SRAM2.cpp` is **NOT active**
- However, basic external SRAM support **IS present** via `ext_SRAM_write2()` and `ext_SRAM_read2()`

### External SRAM Chip Specifications (Typical: Microchip 23LC1024)
- **Size**: 128KB (131,072 bytes)
- **Organization**: 1024 pages × 128 bytes
- **Interface**: SPI (up to 20MHz)
- **Access Time**: ~45ns @ 20MHz
- **Mode**: Sequential access mode (set via `ext_SRAM_set_mode()`)

### External SRAM Layout
When external SRAM is used for TX buffering:
- 1024 slots of 128 bytes each
- Addressed from 0x00000 to 0x1FFFF (128KB)
- Packet size tracking stored in internal RAM (`TX_buff_ext_sizes`)

## Critical Memory Bottlenecks

### 1. Ethernet Reassembly Buffers (11.2KB)
**Location**: `source/L1L2_radio.cpp` line 72
```cpp
static unsigned char ethernet_buffer[radio_addr_table_size][1600];
```
- **Size**: 7 × 1,600 = 11,200 bytes
- **Purpose**: Reassemble segmented packets from each of 7 possible radio clients
- **Impact**: Single largest static allocation
- **Criticality**: HIGH - needed for packet reassembly from radio
- **Optimization potential**: Could be reduced to fewer clients or smaller MTU

### 2. TX Internal FIFO (16KB)
**Location**: `source/global_variables.cpp` line 54
```cpp
unsigned char TX_buff_intern_FIFOdata[128][128];
```
- **Size**: 16,384 bytes
- **Purpose**: Primary TX buffer (without external SRAM) or intermediate buffer (with external SRAM)
- **Impact**: Second largest allocation
- **Criticality**: HIGH - essential for TX operation
- **Optimization potential**: Difficult - already optimized for 128-byte radio packets

### 3. RX FIFO (8KB)
**Location**: `source/global_variables.cpp` line 41
```cpp
unsigned char RX_FIFO_data[0x2000];
```
- **Size**: 8,192 bytes
- **Purpose**: Radio RX packet buffer
- **Impact**: Third largest allocation
- **Criticality**: HIGH - essential for RX operation
- **Optimization potential**: Limited - sized for burst reception

## Stack and Heap Considerations

With ~18KB available for stack and heap:
- **Main thread stack**: Configured in mbed-OS (typically 4KB default)
- **Interrupt stack**: ~1KB
- **Heap**: Remaining space for dynamic allocations
- **Risk**: Deep call stacks or large temporary allocations could cause stack overflow

### Recommendations:
1. Monitor stack usage during development
2. Use static buffers where possible (already done extensively)
3. Avoid recursive functions
4. Be cautious with large local variables
5. Consider using external SRAM for burst traffic scenarios

## Optimization Opportunities

### If EXT_SRAM_USAGE were enabled:
1. **Cost**: +3,378 bytes of internal RAM for management structures
2. **Benefit**: Smarter FIFO management with 8 priority queues
3. **Recommendation**: Only enable if traffic patterns justify the complexity

### Potential RAM savings:
1. **Reduce radio_addr_table_size from 7 to 4**: Saves ~5KB in ethernet_buffer
2. **Reduce DHCP_ARP_tab_size**: Currently 32, could be 16 (saves 224 bytes)
3. **Use smaller MTU**: If 1600-byte packets aren't needed, use 1024 (saves 4KB)

## Conclusion

The NPR-70 firmware is carefully optimized for the RAM-constrained STM32L432KC microcontroller:

### Without External SRAM:
- **Status**: Tight but functional
- **Static RAM usage**: ~44.4KB out of 63.6KB available
- **Free for stack/heap**: ~18KB
- **Primary limitation**: TX buffer capacity (16KB)
- **Best for**: Low to moderate traffic scenarios

### With External SRAM:
- **Status**: More headroom for TX buffering
- **Static RAM usage**: Same ~44.4KB internal
- **Free for stack/heap**: Same ~18KB
- **TX capacity**: 128KB external + 16KB internal
- **Best for**: High traffic, burst scenarios
- **Trade-off**: SPI latency vs capacity

### Overall Assessment:
The firmware makes excellent use of available RAM through:
- Static allocation strategy (avoids fragmentation)
- Conditional buffer switching based on hardware detection
- Efficient circular buffer implementations
- Minimal dynamic allocation

The system is RAM-constrained but well-optimized for its target application as a packet radio modem.
