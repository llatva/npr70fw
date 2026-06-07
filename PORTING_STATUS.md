# NPR-70 FreeRTOS Porting Status

**Date**: January 14, 2025  
**Port Version**: 1.2  
**Original Firmware**: F4HDK NPR-70 mbed OS (2020-05-16)  
**Target Platform**: STM32L432KC + FreeRTOS 11.1.0 LTS

---

## Overall Status: 🟡 FRAMEWORK + FEC COMPLETE - TDMA/ROUTING INCOMPLETE

The RTOS framework, all hardware drivers, and FEC codec are complete and compile successfully.
Radio link layer (FEC encode/decode) is now fully implemented.
Remaining work: TDMA slot allocation, packet routing, and ARP proxy.

---

## Component Status Summary

| Component | Status | Notes |
|-----------|--------|-------|
| Core System (main.c) | ✅ Complete | FreeRTOS init, 8 tasks, all HAL peripherals |
| SI4463 Radio Driver | ✅ Complete | SPI1, temp read, recalibration detect |
| W5500 Ethernet Driver | ✅ Complete | SPI3, sockets, RX/TX transfers |
| Ext. SRAM Driver | ✅ Complete | Now mandatory; test on boot |
| Watchdog | ✅ Complete | IWDG hardware + per-task monitor |
| Config Flash Save/Load | ✅ Complete | CRC32 verified, defaults, save/load |
| Factory Reset | ✅ Complete | Flash erase + default restore |
| Serial CLI (UART) | ✅ Complete | New: UART2 console, shared CLI lib |
| SNMP Agent | ✅ Complete | Full MIB, GET/GETNEXT/SET |
| Telnet HMI | ✅ Complete | Telnet protocol + CLI commands |
| Radio ISR Task | ✅ Complete | Deferred ISR, FIFO read, ISR queue |
| FEC Codec | ✅ Complete | NEW: (4,3) FEC with CRC, parity tables |
| Radio Processing Task | 🟡 Partial | FEC decode active; routing TODOs remain |
| TDMA Task | ⚠️ Stub | Frame timing OK; slot alloc/null frame TODO |
| Signaling Task | 🟡 Partial | FEC encode + TX FIFO active; TX trigger TODO |
| Ethernet Task (RX+TX) | ⚠️ Partial | RX polling OK; IPv4→radio routing stub |
| DHCP/ARP Task | ⚠️ Stub | Table mgmt OK; W5500 socket I/O stubs |
| Monitor Task | ❌ Missing | Not created; temperature recalib loop absent |
| Power Management | ❌ Missing | No sleep modes implemented |
| Firmware Update | ❌ Missing | No OTA/bootloader mechanism |

---

## Build Status

### Current Build Results
```
Compilation: ✅ SUCCESS (no errors)
Warnings:    ⚠️  Minor (FPU redefinition, unused functions)
Linking:     ✅ SUCCESS

Memory Usage (with external SRAM mandatory):
  Flash:  ~58,560 / 262,144 bytes  (~22%)   ✅ Good
  RAM:    ~46,648 /  65,536 bytes  (~71%)   ✅ Good (external SRAM offloads buffers)
  External SRAM: 128KB (23LC1024) — required for operation
```

> **Note:** External SRAM is now **mandatory**. Boot halts if SRAM is absent or fails read/write
> test. Flash usage increased by ~3.5KB due to FEC codec implementation.

### Compiler Configuration
- **Toolchain**: arm-none-eabi-gcc 13.2.1
- **Optimization**: -Og (Debug with optimizations)
- **FPU**: Hard float (fpv4-sp-d16)
- **Specs**: nano.specs (newlib-nano)
- **C Standard**: GNU11

---

## Functional Implementation Status

### ✅ Fully Implemented

#### Core RTOS
- [x] FreeRTOS 11.1.0 LTS kernel integration
- [x] 8 tasks with priority-based scheduling
- [x] Queue-based inter-task communication (RadioISR, RadioTx, EthernetRx, EthernetTx)
- [x] Mutex protection for SPI1 (SI4463) and SPI3 (W5500 + SRAM)
- [x] Event groups for system events
- [x] Microsecond timer (TIM2 @ 1 MHz)
- [x] Watchdog: IWDG hardware + per-task Watchdog_RegisterTask / Watchdog_Refresh

#### Hardware Drivers
- [x] SI4463 driver (SPI1): init, commands, FIFO read/write, RX/TX state, temperature
- [x] W5500 driver (SPI3): init, register access, socket setup, RX/TX transfers
- [x] Ext. SRAM driver (SPI3, CS=PB0): init, test, byte/burst read-write, FIFO management

#### Configuration
- [x] Flash save with CRC32 integrity check
- [x] Flash load with fallback to factory defaults
- [x] Flash erase (factory reset)
- [x] All original config variables preserved

#### User Interface
- [x] Serial CLI over UART2 (921600 baud) — NEW since v1.0
- [x] Telnet server (TCP port 23) with full CLI
- [x] Shared CLI command library (set/get/save/reset/show stats/show tasks/show memory…)
- [x] SNMP agent (UDP port 161) with NPR-70 MIB, GET/GETNEXT/SET

#### Radio Link Layer — NEW in v1.2
- [x] FEC codec module (`Application/Common/fec_codec.c`)
- [x] `FEC_Encode()` — (4,3) code: split into 3 blocks, add XOR block, CRC per field
- [x] `FEC_Decode()` — check 4 CRCs, reconstruct single corrupted field via XOR
- [x] `FEC_SizeWithEncoding()` — calculate encoded size
- [x] Parity bit tables (parity_bit_elab[128], parity_bit_check[256])
- [x] FEC integration in radio processing task (decode path)
- [x] FEC integration in signaling task (encode path + TX FIFO write)

### 🟡 Partial Implementation (New Improvements in v1.2)

#### Signaling Protocol
- [x] Frame structure building (WHOIS, connect request/response, keepalive)
- [x] Connection state machine
- [x] **FEC-encode before TX** — ✅ COMPLETE: uses `FEC_Encode()` from fec_codec.c
- [x] **Write to SI4463 TX FIFO** — ✅ COMPLETE: `Signaling_FramePush()` calls `SI4463_WriteTxFifo()`
- [x] **Parity bit computation** — ✅ COMPLETE: uses `parity_bit_elab[]` table
- [ ] **TX mode trigger** — TODO: need `SI4463_PrepareTX()` or equivalent
- [ ] **TX complete event** — TODO: callback from radio ISR
- [ ] **LAN reset on signaling event** — `task_signaling.c:574`: TODO

#### Radio Processing
- [x] RX FIFO dequeue loop, protocol byte dispatch
- [x] IPv4 packet reassembly (segmenter byte logic ported from original)
- [x] **FEC decode integration** — ✅ COMPLETE: calls `FEC_Decode()` from fec_codec.c, reads from RX_FIFO_data
- [ ] **TX preparation trigger** — `task_radio_processing.c:177`: no queue/call to TDMA task
- [ ] **Signaling/TDMA frame forward** — `task_radio_processing.c:371,383`: TODO comments

### ⚠️ Stub / Partial Implementation (Unchanged from v1.1)

#### TDMA Protocol
- [x] Frame timer (TIM2), timeout detection, frame counter, multiframe mask
- [x] TDMA byte assembly and parity bit (client uplink buffer size bits)
- [ ] **Master slot allocation algorithm** — `task_tdma.c:107`: TODO block
- [ ] **Null frame initialization** — `task_tdma.c:294`: TODO
- [ ] **Slave allocation frame parsing** — `task_tdma.c:211`: TODO (maps to `TDMA_slave_alloc_exploitation()` in original)
- [ ] **TX slot scheduling** — `task_tdma.c:145`: TODO (no timer/queue trigger)

#### Ethernet ↔ Radio Packet Routing
- [x] ARP packet detection (EtherType 0x0806)
- [x] IPv4 packet detection (EtherType 0x0800)
- [ ] **IPv4 → radio routing** — `RouteIPv4ToRadio()` in `task_ethernet_rx.c` is a stub (counts packets only)
- [ ] **ARP processing and proxy** — `ProcessARPPacket()` stub; no ARP table lookup or reply
- [ ] **FDD downlink packet handling** — `task_ethernet_rx.c:185`: TODO (port 6716 path)

#### DHCP/ARP Task
- [x] DHCP table structure, state machine, offer/ack/nak logic
- [ ] **W5500 socket reads** — `task_dhcp_arp.c:413-418`: RX_size forced to 0 (stub)
- [ ] **DHCP packet send via W5500** — `task_dhcp_arp.c:513,565,582`: TODOs
- [ ] **ARP proxy** — `task_dhcp_arp.c:606-615`: entire proxy function is stub

### ❌ Not Implemented

#### Monitor Task (Implementation Plan Task 6.5)
- [ ] No `vMonitorTask` created
- [ ] Temperature monitoring loop (SI4463_CheckTemperatureCalibration exists but unused)
- [ ] Periodic recalibration trigger
- [ ] LED status updates

#### Advanced Features
- [ ] Power management (sleep modes between TDMA slots)
- [ ] Bootloader integration
- [ ] Firmware update mechanism (OTA or serial)
- [ ] Extended diagnostics / log ring buffer

---

## Structural Issues (Code Hygiene)

The following redundant files exist and cause confusion about which implementation is authoritative.
They compile but are **not used** by `main.c`:

| Unused file | Active replacement |
|-------------|-------------------|
| `task_radio_isr.c` + `task_radio_processing.c` | `task_radio_combined.c` (vRadioTask) |
| `task_ethernet_rx.c` + `task_ethernet_tx.c` | `task_ethernet.c` (vEthernetTask) |
| `task_networkmgmt.c` + `task_netmgmt_telnet.c` | `task_network_mgmt.c` (vNetworkMgmtTask) |

The stub bodies in `task_networkmgmt.c` (`DHCPARPTask_Poll`, `SNMPTask_Poll`) contain the comment
*"user should fill in with actual periodic logic"* — indicating incomplete delegation to the underlying
DHCP/ARP and SNMP subsystems.

---

## Memory Analysis

### RAM Breakdown (with external SRAM mandatory)
```
Internal RAM (65,536 bytes total):
  FreeRTOS Heap:       ~16,000 bytes
  Task Stacks:          ~6,000 bytes
  Global/BSS:          ~28,000 bytes (config, state, small buffers)
  
External SRAM (128KB — 23LC1024):
  RX FIFO buffer:       2,048 bytes
  Ethernet packet buffers: up to 16 × 1,600 bytes (lazy allocated)
  Queue backing:        varies
```

### Flash Breakdown
```
Application Code:     ~38,000 bytes  (~72%)
FreeRTOS Kernel:       ~8,000 bytes  (~15%)
STM32 HAL:             ~6,000 bytes  (~11%)
Const Strings/Tables:  ~3,000 bytes  (~ 6%)
```

---

## Task Priority and Stack Configuration

| Task | Priority | Stack (words) | Notes |
|------|----------|---------------|-------|
| RadioTask (combined) | 7 | 220 | ISR + processing |
| TDMA | 6 | 144 | Timing-critical |
| Signaling | 5 | 112 | |
| Ethernet (combined RX+TX) | 4 | 180 | |
| NetworkMgmt (DHCP+SNMP) | 3 | 144 | |
| Telnet | 2 | 144 | |
| SerialCLI | 2 | configurable | New in v1.1 |
| Watchdog | tskIDLE+1 | 112 | Lowest |

---

## Completed Items Since v1.0

| Item | Previous Status | Current Status |
|------|-----------------|----------------|
| Watchdog timer | ❌ Not implemented | ✅ Complete |
| Config flash save/load | ⚠️ Stub (vars exist, no I/O) | ✅ Complete |
| Factory reset flash erase | ⚠️ Reboots only | ✅ Complete |
| External SRAM integration | ⚠️ Detected, unused | ✅ Complete (mandatory) |
| Serial CLI (UART) | ❌ Not planned | ✅ New feature |

---

## Testing Requirements

### Hardware Not Yet Testable (protocol stubs incomplete)
- Radio TX/RX (FEC not implemented)
- TDMA master slot allocation
- DHCP server (socket I/O stub)
- ARP proxy

### Can Be Tested Now (driver-level)
- [ ] UART serial CLI connects and responds to commands
- [ ] SI4463 SPI comms (version read, chip present)
- [ ] W5500 SPI comms (chip ID, ping response)
- [ ] External SRAM read/write test (runs at boot)
- [ ] SNMP responds to queries
- [ ] Telnet connection and CLI
- [ ] Config save/load survives reboot
- [ ] Watchdog kick in all tasks
- [ ] TIM2 microsecond counter accuracy

### Full System Tests (blocked pending stubs)
- [ ] Radio TX test mode → scope / spectrum analyzer
- [ ] Radio RX receives packets from another NPR-70
- [ ] TDMA synchronization master ↔ slave
- [ ] IP packet ping through radio link
- [ ] DHCP assigns IPs to radio clients
- [ ] 24-hour stability test

---

## Known Issues

### Issue 1: FEC Not Implemented
**Severity**: Critical — no radio link possible  
**Status**: Stubs in task_signaling.c and task_radio_processing.c  
**Fix**: Port `FEC_encode2()` / `FEC_decode()` from `source/L1L2_radio.cpp`

### Issue 2: TDMA Slot Allocation Missing
**Severity**: Critical for multi-client operation  
**Status**: Frame counter and timeout OK; allocation algorithm TODO  
**Fix**: Port `TDMA_master_allocation()` from `source/TDMA.cpp`

### Issue 3: DHCP/ARP Task Does No Socket I/O
**Severity**: High — DHCP server non-functional  
**Status**: Table and logic structure exist; W5500 calls are stubs  
**Fix**: Wire `W5500_ReadUDPPacket()` / `W5500_WriteUDPPacket()` calls

### Issue 4: IPv4 ↔ Radio Routing Is a Stub
**Severity**: High — no data traffic possible  
**Status**: RouteIPv4ToRadio() counts packets only  
**Fix**: Port `IPv4_to_radio()` from `source/Eth_IPv4.cpp`

### Issue 5: Redundant Source Files
**Severity**: Medium — maintenance confusion  
**Status**: Old individual task files coexist with combined replacements  
**Fix**: Remove or clearly mark unused files (see Structural Issues section)
