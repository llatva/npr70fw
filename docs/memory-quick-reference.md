# NPR-70 Memory Analysis - Quick Reference

## Platform Summary
- **MCU**: STM32L432KC (Cortex-M4F)
- **Internal RAM**: 64KB (63.6KB usable)
- **Flash**: 256KB
- **Optional**: External 128KB SPI SRAM

## Memory Usage at a Glance

### Total Internal RAM Breakdown

```
┌──────────────────────────────────────────┐
│ Total RAM: 64KB                          │
├──────────────────────────────────────────┤
│ System Reserved:        0.4KB   ( 0.6%) │
│ Static Buffers:        44.4KB   (71.0%) │
│ Stack/Heap/Dynamic:    ~18KB    (28.4%) │
└──────────────────────────────────────────┘
```

**⚠️ RAM Status: CONSTRAINED** (71% used for static allocations)

## Top 5 Memory Consumers

| Rank | Buffer | Size | % of Total RAM | Purpose |
|------|--------|------|----------------|---------|
| 1 | TX Internal FIFO | 16,384 bytes | 25.6% | Transmit packet buffer |
| 2 | Ethernet Reassembly | 11,200 bytes | 17.5% | 7 client reassembly buffers |
| 3 | RX FIFO | 8,192 bytes | 12.8% | Receive packet buffer |
| 4 | mbed-OS System | ~3,000 bytes | 4.7% | RTOS overhead |
| 5 | Virtual Channel | 1,614 bytes | 2.5% | RTP gateway buffer |
| | **Top 5 Total** | **40,390 bytes** | **63.1%** | |
| | **All Other** | **5,086 bytes** | **7.9%** | Config, tables, etc. |
| | **Available** | **~18,000 bytes** | **28.4%** | Stack, heap, dynamic |

## Configuration Comparison

### Without External SRAM
```
Internal RAM: 44.4KB static + ~18KB dynamic
TX Capacity:  16KB (128 packets)
RX Capacity:  8KB
Status:       Tight but functional
Best for:     Low to moderate traffic
```

### With External SRAM
```
Internal RAM: 44.4KB static + ~18KB dynamic (SAME)
TX Capacity:  128KB external + 16KB internal (8x more)
RX Capacity:  8KB (same)
Status:       Better TX headroom, same internal pressure
Best for:     High traffic, burst scenarios
```

**Key Point**: External SRAM adds TX capacity but does NOT reduce internal RAM usage!

## Buffer Allocation by Category

### Communication Buffers (35.9KB - 56.4%)
- RX FIFO: 8,192 bytes
- TX Internal: 16,384 bytes
- TX Size Tracking: 1,024 bytes
- Ethernet Reassembly: 11,200 bytes (7 × 1600)
- Virtual Channel: 1,600 bytes
- DHCP Answer: 400 bytes

### Protocol Buffers (1.2KB - 1.9%)
- TDMA Internal: 384 bytes
- TDMA Signaling: 300 bytes
- Radio Layer: 874 bytes (trash, data_RX, rframe_TX)

### Tables & Configuration (1.7KB - 2.7%)
- Radio Address Table: 343 bytes
- DHCP/ARP Tables: 848 bytes
- TDMA Tables: 133 bytes
- Parity Tables: 384 bytes

### Driver Buffers (0.5KB - 0.8%)
- SI4463 Driver: 534 bytes

### System & Misc (5.1KB - 8.0%)
- mbed-OS: ~3,000 bytes
- HMI/Telnet: 220 bytes
- Network Config: 64 bytes

## Critical Paths

### RX Path (Always Internal RAM)
```
Radio → SI4463 → RX_FIFO (8KB) → Reassembly (11.2KB) → Ethernet
```
**Total RX buffering: ~19.2KB internal RAM**

### TX Path (Configuration Dependent)

**Without External SRAM:**
```
Ethernet → TX_intern_FIFO (16KB) → Radio
```
**Capacity: 16KB internal RAM**

**With External SRAM:**
```
Ethernet → External SRAM (128KB) → Radio
```
**Capacity: 128KB external + 16KB internal**

## Memory Optimization Opportunities

### High Impact (4-5KB savings each)
1. **Reduce client count** (radio_addr_table_size: 7→4)
   - Saves: 4.8KB in ethernet_buffer
   - Impact: Fewer simultaneous radio clients

2. **Reduce MTU** (ethernet_buffer: 1600→1024 bytes)
   - Saves: 4KB
   - Impact: Smaller max packet size

### Medium Impact (1-2KB savings)
3. **Reduce TX buffer** (TX_buff_intern_FIFOdata: 128→96 slots)
   - Saves: 4KB
   - Impact: Lower TX capacity (only viable with external SRAM)

### Low Impact (<1KB savings)
4. **Reduce DHCP table** (DHCP_ARP_tab_size: 32→16)
   - Saves: 224 bytes
   - Impact: Fewer DHCP clients

## Stack/Heap Budget

With ~18KB available:
- **Main stack**: ~4KB (mbed-OS default)
- **ISR stack**: ~1KB
- **Heap**: ~13KB remaining
- **Margin**: Very tight!

### Recommendations:
1. ✓ Avoid deep recursion
2. ✓ Limit local variable sizes
3. ✓ Use static buffers (already done)
4. ✓ Monitor stack watermark
5. ⚠️ Consider external SRAM for burst scenarios

## Quick Comparison Matrix

| Feature | No Ext SRAM | With Ext SRAM |
|---------|-------------|---------------|
| Internal RAM | 44.4KB | 44.4KB ← SAME |
| TX Capacity | 16KB | 144KB (128KB+16KB) |
| TX Speed | Fast | Slower (SPI) |
| Burst Handling | Limited | Excellent |
| Cost | Lower | +$2 chip |
| Complexity | Simple | More complex |

## Common Misconceptions

❌ **WRONG**: "External SRAM reduces internal RAM usage"
✓ **CORRECT**: "External SRAM adds TX capacity while internal RAM stays the same"

❌ **WRONG**: "With 128KB external SRAM, we have plenty of memory"
✓ **CORRECT**: "External SRAM helps TX only; internal RAM is still constrained at 44.4KB used"

❌ **WRONG**: "RX path benefits from external SRAM"
✓ **CORRECT**: "RX path always uses internal RAM for speed; external SRAM is TX only"

## When to Use External SRAM

### ✓ Use External SRAM if:
- Experiencing TX buffer overruns
- High burst traffic scenarios
- Multiple simultaneous active clients
- Need to queue many packets
- Traffic is >100 kbps sustained

### ✗ Don't need External SRAM if:
- Traffic is light (<50 kbps)
- Single client or few clients
- Cost is primary concern
- Minimal power consumption required

## Technical Specifications

### Internal RAM (STM32L432KC)
- **Total**: 64KB
- **Start**: 0x20000000
- **End**: 0x20010000
- **Reserved**: 0x188 bytes (system)
- **Available**: ~63.6KB

### External SRAM (Typical: 23LC1024)
- **Size**: 128KB
- **Interface**: SPI (up to 20MHz)
- **Organization**: 1024 × 128 bytes
- **Access**: ~5-10μs per operation
- **Detection**: Auto-detect at boot via test write/read

## Related Documents

- **[memory-analysis.md](memory-analysis.md)** - Comprehensive memory analysis
- **[buffer-comparison.md](buffer-comparison.md)** - Detailed buffer comparison

---

**Last Updated**: Based on firmware version `2020_05_16-hzb-20250618`
**Target**: STM32L432KC (NUCLEO_L432KC board)
**Build**: mbed-OS with GCC_ARM toolchain
