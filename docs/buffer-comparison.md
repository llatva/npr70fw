# Buffer Comparison: External SRAM vs No External SRAM

## Overview

This document provides a detailed side-by-side comparison of buffer allocations and usage patterns between the two operational modes of the NPR-70 firmware.

## Buffer Allocation Comparison Table

| Buffer Name | Location | No Ext SRAM | With Ext SRAM | Difference | Notes |
|-------------|----------|-------------|---------------|------------|-------|
| **RX FIFO** | global_variables.cpp:41 | 8,192 bytes | 8,192 bytes | None | Always in internal RAM |
| **TX Internal FIFO** | global_variables.cpp:54 | 16,384 bytes | 16,384 bytes | None | Internal buffer always allocated |
| **TX External Storage** | External SPI SRAM | 0 bytes | 131,072 bytes | +128KB | External chip only |
| **TX Size Tracking** | global_variables.cpp:59 | 1,024 bytes | 1,024 bytes | None | Always allocated |
| **TDMA Internal** | global_variables.cpp:64 | 384 bytes | 384 bytes | None | No change |
| **TDMA Signaling** | global_variables.h:90 | 300 bytes | 300 bytes | None | No change |
| **Ethernet Reassembly** | L1L2_radio.cpp:72 | 11,200 bytes | 11,200 bytes | None | Critical for packet reassembly |
| **Radio Layer Static** | L1L2_radio.cpp | 916 bytes | 916 bytes | None | trash, data_RX, rframe_TX, etc. |
| **Virtual Channel** | Virt_Chan.cpp | 1,614 bytes | 1,614 bytes | None | RX_data buffer |
| **SI4463 Driver** | SI4463.cpp | 534 bytes | 534 bytes | None | Radio driver buffers |
| **DHCP/ARP** | DHCP_ARP.cpp | 848 bytes | 848 bytes | None | Network tables |
| **TDMA Tables** | TDMA.cpp | 133 bytes | 133 bytes | None | Management tables |
| **Config/State** | global_variables.cpp | 343 bytes | 343 bytes | None | Configuration variables |
| **Parity Tables** | global_variables.cpp | 384 bytes | 384 bytes | None | Lookup tables |
| **HMI** | Various | 220 bytes | 220 bytes | None | Telnet interface |
| **mbed-OS** | System | ~3,000 bytes | ~3,000 bytes | None | RTOS overhead |
| **ExtSRAM Mgmt** | ext_SRAM2.cpp | 0 bytes | 0 bytes* | None | *Currently disabled |
| | | | | | |
| **TOTAL Internal RAM** | | **45,476 bytes** | **45,476 bytes** | **Same** | ~44.4KB |
| **TOTAL External RAM** | | **0 bytes** | **131,072 bytes** | **+128KB** | Off-chip |
| **Available Stack/Heap** | | **~18KB** | **~18KB** | **Same** | From 63.6KB total |

## Key Observations

### 1. Internal RAM Usage is IDENTICAL
Both configurations use the same amount of internal RAM (~44.4KB). The presence of external SRAM does NOT reduce internal RAM consumption.

### 2. External SRAM Adds Capacity, Not Reduces Usage
External SRAM provides additional TX buffer space (128KB) but doesn't free up internal RAM. Think of it as adding an external hard drive to a computer - it doesn't increase RAM, it increases storage.

### 3. TX Internal FIFO Remains Allocated
Even with external SRAM, the 16KB internal TX FIFO (`TX_buff_intern_FIFOdata[128][128]`) remains allocated. It serves as a fast intermediate buffer.

## Buffer Usage Patterns

### TX Buffer Flow - Without External SRAM

```
Ethernet Interface
      ↓
[segment_and_push() in L1L2_radio.cpp]
      ↓
TX_FIFO_write_global()
      ↓
TX_intern_FIFO_write()
      ↓
TX_buff_intern_FIFOdata[128][128]  ← 16KB Internal RAM
      ↓
TX_intern_FIFO_read()
      ↓
SI4463 Radio Transmit
```

**Characteristics**:
- Direct write to internal RAM
- Fast access (direct memory)
- Limited capacity (128 packets × 128 bytes = 16KB)
- Circular buffer with WR/RD pointers
- Risk of overrun during traffic bursts

### TX Buffer Flow - With External SRAM

```
Ethernet Interface
      ↓
[segment_and_push() in L1L2_radio.cpp]
      ↓
TX_FIFO_write_global()
      ↓
TX_ext_FIFO_write()
      ↓
ext_SRAM_write2() via SPI
      ↓
External SPI SRAM (128KB)  ← Off-chip storage
      ↓
ext_SRAM_read2() via SPI
      ↓
TX_buff_intern_FIFOdata (may be used as intermediate)
      ↓
SI4463 Radio Transmit
```

**Characteristics**:
- Write through SPI interface
- Slower access (SPI protocol overhead)
- Large capacity (1024 packets × 128 bytes = 128KB)
- Packet size tracking in internal `TX_buff_ext_sizes[1024]`
- Better handling of traffic bursts
- Internal buffer may still be used for fast access

## RX Buffer Flow (Same for Both Configurations)

```
SI4463 Radio Receive
      ↓
SI4463_HW_interrupt()
      ↓
RX_FIFO_data[0x2000]  ← 8KB Internal RAM
      ↓
radio_RX_FIFO_dequeue()
      ↓
ethernet_buffer[7][1600]  ← 11.2KB Internal RAM (reassembly)
      ↓
W5500 Ethernet Interface
```

**No difference between configurations** - RX path always uses internal RAM.

## Performance Characteristics

### Write Performance

| Operation | No Ext SRAM | With Ext SRAM | Notes |
|-----------|-------------|---------------|-------|
| **TX Write Speed** | ~50-100 ns | ~5-10 μs | SPI overhead ~100x slower |
| **TX Capacity** | 16KB (128 packets) | 128KB (1024 packets) | 8x more capacity |
| **Overrun Risk** | High during bursts | Low | Better burst handling |
| **CPU Usage** | Low | Higher | SPI transfers consume CPU cycles |

### Read Performance

| Operation | No Ext SRAM | With Ext SRAM | Notes |
|-----------|-------------|---------------|-------|
| **RX Speed** | Same | Same | RX always uses internal RAM |
| **RX Capacity** | 8KB | 8KB | No difference |

## Memory Pressure Comparison

### Without External SRAM

```
┌────────────────────────────────────┐
│  STM32L432KC Internal RAM (64KB)   │
├────────────────────────────────────┤
│  System Reserved         0.4KB     │
├────────────────────────────────────┤
│  Static Allocations     44.4KB     │
│  ├─ RX FIFO              8.0KB     │
│  ├─ TX Internal FIFO    16.0KB  ◄── Primary TX buffer
│  ├─ Ethernet Buffers    11.2KB     │
│  ├─ Other Buffers        9.2KB     │
├────────────────────────────────────┤
│  Stack/Heap/Dynamic     ~18KB      │ ◄── Limited headroom
└────────────────────────────────────┘
```

**Memory Pressure**: ⚠️ HIGH (71% used for static buffers)

### With External SRAM

```
┌────────────────────────────────────┐
│  STM32L432KC Internal RAM (64KB)   │
├────────────────────────────────────┤
│  System Reserved         0.4KB     │
├────────────────────────────────────┤
│  Static Allocations     44.4KB     │ ◄── Same internal usage
│  ├─ RX FIFO              8.0KB     │
│  ├─ TX Internal FIFO    16.0KB  ◄── Now intermediate buffer
│  ├─ Ethernet Buffers    11.2KB     │
│  ├─ Other Buffers        9.2KB     │
├────────────────────────────────────┤
│  Stack/Heap/Dynamic     ~18KB      │ ◄── Same headroom
└────────────────────────────────────┘

┌────────────────────────────────────┐
│  External SPI SRAM (128KB)         │
├────────────────────────────────────┤
│  TX Buffer Storage     128KB     ◄── Primary TX storage
└────────────────────────────────────┘
```

**Memory Pressure**: ⚠️ HIGH internally (same 71%), but more TX capacity externally

## Buffer Selection Logic

### Code Path in `TX_FIFO_write_global()`

**Location**: `source/L1L2_radio.cpp` line 476

```cpp
void TX_FIFO_write_global(unsigned char* data, int size) {
    if (is_SRAM_ext == 1) {
        TX_ext_FIFO_write(data, size);      // External SRAM path
    } else {
        TX_intern_FIFO_write(data, size);   // Internal RAM path
    }
}
```

### Detection at Boot

**Location**: `source/main.cpp` line 119

```cpp
is_SRAM_ext = ext_SRAM_detect();
```

This single runtime detection determines the entire TX buffer strategy for the life of the firmware run.

## Critical Differences Summary

| Aspect | No External SRAM | With External SRAM |
|--------|------------------|-------------------|
| **Internal RAM** | 44.4KB static | 44.4KB static (SAME) |
| **TX Capacity** | 16KB | 128KB external + 16KB internal |
| **TX Write Speed** | Fast (direct RAM) | Slower (SPI interface) |
| **Burst Handling** | Limited | Excellent |
| **Complexity** | Simpler code path | More complex management |
| **Hardware Cost** | No additional chip | ~$2 external SRAM chip |
| **Power** | Lower | Slightly higher (SPI activity) |
| **Best Use Case** | Low-moderate traffic | High traffic, burst scenarios |

## Ethernet Reassembly Buffer Deep Dive

This is the single largest buffer allocation and is critical to understand:

**Location**: `source/L1L2_radio.cpp` line 72
```cpp
static unsigned char ethernet_buffer[radio_addr_table_size][1600];
```

### Why So Large?

- **Purpose**: Reassemble segmented Ethernet frames from radio packets
- **Size**: 7 clients × 1600 bytes = 11,200 bytes (11.2KB)
- **Reason**: Radio packets are limited to ~250 bytes, but Ethernet frames can be up to 1518 bytes
  - MTU of 1500 bytes + 14 byte Ethernet header + 4 byte VLAN (optional)
  - Firmware uses 1600 to be safe

### Why Per-Client?

- Each of 7 possible radio clients may have an in-progress segmented packet
- Can't share buffer between clients or packets would corrupt each other
- Radio packets arrive out of order and must be reassembled

### Impact on Both Configurations

This buffer is in **internal RAM** for both configurations because:
1. RX path needs fast reassembly
2. External SRAM would be too slow for real-time packet processing
3. Can't afford SPI latency on receive path

## Optimization Strategies

### If RAM Pressure Becomes Critical

#### Strategy 1: Reduce Client Count
```cpp
#define radio_addr_table_size 4  // Instead of 7
```
- **Saves**: 3 × 1600 = 4,800 bytes (4.8KB)
- **Impact**: Support fewer simultaneous radio clients
- **Risk**: Low if network topology supports it

#### Strategy 2: Reduce MTU
```cpp
static unsigned char ethernet_buffer[7][1024];  // Instead of 1600
```
- **Saves**: 7 × 576 = 4,032 bytes (4KB)
- **Impact**: Limit Ethernet frame size to 1024 bytes
- **Risk**: May break some protocols expecting standard MTU

#### Strategy 3: Enable EXT_SRAM_USAGE (Advanced)
```cpp
#define EXT_SRAM_USAGE  // In global_variables.h
```
- **Cost**: +3,378 bytes internal RAM
- **Benefit**: Better external SRAM management with 8 priority FIFOs
- **Recommendation**: Only if traffic patterns justify complexity

## Conclusion

The key insight is that **external SRAM adds TX capacity without reducing internal RAM usage**. It's a capacity enhancement, not a RAM-saving feature. Both configurations face the same internal RAM constraints (~18KB available for stack/heap), but external SRAM configuration can handle much higher TX traffic loads without buffer overruns.

### Decision Matrix

Choose **No External SRAM** when:
- ✓ Traffic is light to moderate
- ✓ Cost is a concern
- ✓ Simplicity is preferred
- ✓ Power consumption must be minimized

Choose **With External SRAM** when:
- ✓ High traffic or burst scenarios
- ✓ TX buffer overruns are occurring
- ✓ Multiple simultaneous connections
- ✓ Better reliability needed
- ✗ Can accept slightly higher latency on TX path
