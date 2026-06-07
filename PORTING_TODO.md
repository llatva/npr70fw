# NPR-70 FreeRTOS Port — Remaining TODO List

**Date**: June 7, 2026  
**Status**: Framework complete, protocol logic incomplete  
**Blocking full hardware test**: items marked 🔴 (critical)

Reference originals are in `source/` (mbed C++ code).  
All new C implementations go in `Application/Tasks/` or `Application/Services/`.

---

## Priority 1 — Radio Link (nothing works without these)

### 🔴 TODO-1: Port FEC encode/decode

**Files to edit**: `Application/Tasks/task_radio_processing.c`, `Application/Tasks/task_signaling.c`  
**Reference**: `source/L1L2_radio.cpp` — `FEC_encode2()` (line 259), `FEC_decode()` (line 304), `size_w_FEC_compute()` (line 389)

The FEC codec uses a (3,1) repetition code: every input byte is written three times.
Decoding uses majority vote per bit.

- [ ] Port `FEC_encode2(data_in, data_out, size_in)` → plain C, no mbed deps
- [ ] Port `FEC_decode(data_out, size_in, micro_BER*)` → returns decoded length or ≤0 on error
- [ ] Port `size_w_FEC_compute(size_wo_FEC)` helper
- [ ] Replace stub in `task_signaling.c:780-781`: `size_w_FEC = size_wo_FEC` → call `FEC_encode2()`
- [ ] Replace stub in `task_radio_processing.c:302-315`: placeholder FEC decode → call `FEC_decode()`
- [ ] Add `micro_BER` accumulation and expose via SNMP/CLI stats

---

### 🔴 TODO-2: Port parity bit computation for TDMA/signaling bytes

**Files to edit**: `Application/Tasks/task_signaling.c`, `Application/Tasks/task_tdma.c`  
**Reference**: `source/TDMA.cpp` — `parity_bit_elab[]` lookup table (used in `TDMA_byte_elaboration()`); `source/L1L2_radio.cpp` — `parity_bit_check[]` table  

- [ ] Add `parity_bit_elab[128]` lookup table (even parity over 7-bit input → 8th bit)
- [ ] Fix `task_signaling.c:668` TODO — replace with lookup table call
- [ ] Verify TDMA byte parity in `task_tdma.c` `TDMA_ByteElaboration()` uses the same table

---

### 🔴 TODO-3: Wire SI4463 TX FIFO write in signaling task

**File to edit**: `Application/Tasks/task_signaling.c`  
**Reference**: `source/L1L2_radio.cpp` — `TxFIFO_write()`, `source/signaling.cpp` — `radio_send_signalisation_frame()`

After FEC encoding (TODO-1), the signaling frame must be written to the SI4463 TX FIFO
and TX mode triggered. Currently lines 787–820 are all TODO stubs.

- [ ] Call `SI4463_WriteTxFIFO(hsi4463, rframe_TX, rframe_length)` at `task_signaling.c:787`
- [ ] Implement `SI4463_PrepareTX()` trigger (check `si4463_driver.c` for existing `SI4463_prepa_TX_1()`)
- [ ] Handle FIFO space check before write (`task_signaling.c:775`)
- [ ] Add TX complete event / callback from radio ISR to signaling task (via queue or event group bit)

---

### 🔴 TODO-4: Implement TDMA master slot allocation algorithm

**File to edit**: `Application/Tasks/task_tdma.c`  
**Reference**: `source/TDMA.cpp` — `TDMA_master_allocation()` (search for "master_allocated_slots"), `TDMA_byte_elaboration()`

The master must distribute uplink slots to clients based on their reported buffer sizes.

- [ ] Port `TDMA_master_allocation()`: iterates `tdma_table_uplink_usage[]`, computes slot count per client, fills `tdma_table_slots[]`
- [ ] Fill in TODO block at `task_tdma.c:107` with the allocation loop
- [ ] Trigger TDMA NULL frame transmission to broadcast the allocation (`task_tdma.c:145`)
- [ ] Update `master_allocated_slots` counter

---

### 🔴 TODO-5: Implement TDMA slave allocation frame parsing

**File to edit**: `Application/Tasks/task_tdma.c`  
**Reference**: `source/TDMA.cpp` — `TDMA_slave_alloc_exploitation(data, size)`

When in client mode, parse the TDMA allocation frame received from master to extract
`my_multiframe_mask`, `my_multiframe_id`, and timing offset.

- [ ] Port `TDMA_slave_alloc_exploitation()` into the TODO block at `task_tdma.c:211`
- [ ] Extract slot assignment fields from allocation frame
- [ ] Update `slave_alloc_rx_age` and scheduling variables

---

### 🔴 TODO-6: Implement TDMA null frame initialization and transmission

**File to edit**: `Application/Tasks/task_tdma.c`  
**Reference**: `source/L1L2_radio.cpp` — null frame building and `TxFIFO_write()`

- [ ] Fill TODO at `task_tdma.c:294`: build a null-data TDMA frame using `TDMA_ByteElaboration()`
- [ ] Write null frame to SI4463 TX FIFO via `SI4463_WriteTxFIFO()`
- [ ] Schedule at correct slot boundary using TIM2 microsecond timer

---

## Priority 2 — Ethernet ↔ Radio Data Path

### 🟠 TODO-7: Implement IPv4 → radio routing in Ethernet task

**File to edit**: `Application/Tasks/task_ethernet.c` (function `RouteIPv4ToRadio()`)  
**Reference**: `source/Eth_IPv4.cpp` — `IPv4_to_radio()`, `LookupClientIDFromIP()`

- [ ] Look up destination IP in client table (`CONF_radio_addr_table_IP_begin[]`) to get LID
- [ ] Segment payload into radio frames (max payload per frame depends on modulation rate)
- [ ] FEC-encode each segment (TODO-1 prerequisite)
- [ ] Push segments to `xRadioTxQueue` for the radio task to transmit

---

### 🟠 TODO-8: Implement ARP proxy in Ethernet task

**File to edit**: `Application/Tasks/task_ethernet.c` (function `ProcessARPPacket()`)  
**Reference**: `source/DHCP_ARP.cpp` — `ARP_process()`

The modem must reply to ARP requests on behalf of radio clients.

- [ ] Parse ARP request (target IP, sender MAC/IP)
- [ ] Look up target IP in client/DHCP table
- [ ] If found, craft ARP reply using modem MAC as hardware address
- [ ] Send ARP reply via `W5500_SendRaw()`

---

### 🟠 TODO-9: Implement FDD downlink packet handling

**File to edit**: `Application/Tasks/task_ethernet.c`  
**Reference**: `source/Eth_IPv4.cpp` — FDD downlink path (UDP port 6716 / `FDD_DOWN_PORT`)

- [ ] On receipt of UDP dst port 6716, extract payload and inject into radio RX path
- [ ] Fill TODO at `task_ethernet_rx.c:185` (or equivalent in `task_ethernet.c`)

---

## Priority 3 — DHCP Server and ARP Task

### 🟠 TODO-10: Wire W5500 socket reads in DHCP/ARP task

**File to edit**: `Application/Tasks/task_dhcp_arp.c`

Currently `RX_size` is forced to 0 (stub). No DHCP packets are ever received.

- [ ] Replace stub at `task_dhcp_arp.c:413-418` with `W5500_GetRxSize(pw5500, DHCP_SOCKET)`
- [ ] Read UDP packet with `W5500_ReadUDP(pw5500, DHCP_SOCKET, data, &size, src_ip, &src_port)`
- [ ] Verify `W5500_ReadUDP()` exists in `w5500_driver.c`; add if missing

---

### 🟠 TODO-11: Implement DHCP packet send

**File to edit**: `Application/Tasks/task_dhcp_arp.c`

DHCP OFFER, ACK, and NAK are built but never sent (TODOs at lines 513, 565, 582).

- [ ] Call `W5500_SendUDP(pw5500, DHCP_SOCKET, response, len, dst_ip, DHCP_CLIENT_PORT)` at each send site
- [ ] Broadcast DHCP OFFER/ACK to 255.255.255.255 per RFC 2131

---

### 🟠 TODO-12: Implement ARP proxy in DHCP/ARP task

**File to edit**: `Application/Tasks/task_dhcp_arp.c`  
**Reference**: `source/DHCP_ARP.cpp` — `ARP_process()`

- [ ] Fill TODO stub at `task_dhcp_arp.c:606-615`
- [ ] Monitor W5500 RAW socket for ARP frames
- [ ] Reply to ARP requests for known DHCP-allocated IPs on behalf of radio clients

---

## Priority 4 — Monitor Task (new task)

### 🟡 TODO-13: Create Monitor task

**File to create**: `Application/Tasks/task_monitor.c` / `task_monitor.h`  
**Reference**: `source/main.cpp` (temperature check loop), `source/SI4463.cpp` (recalibration)

This task was specified in IMPLEMENTATION_PLAN.md (Task 6.5) but never created.

- [ ] Create `vMonitorTask(void *pvParameters)`
- [ ] Register with `xTaskCreate()` in `Core/Src/main.c` (lowest priority, 128-word stack)
- [ ] Periodic loop (every 30 s): call `SI4463_CheckTemperatureCalibration(hsi4463, &needs_recal)`
- [ ] If `needs_recal`, trigger recalibration (SI4463 property set sequence) and log
- [ ] Update LED/GPIO status pin to indicate system health
- [ ] Collect and log stack high-water marks for all tasks (optional debug)

---

## Priority 5 — Code Hygiene

### 🟡 TODO-14: Remove or archive redundant task files

The following files compile but are **not used** by `main.c`. They contain outdated or
duplicate logic that can mislead future developers.

**Option A (recommended)**: Delete unused files  
**Option B**: Move to `Application/archive/` with a README note

Files to handle:
- [ ] `Application/Tasks/task_radio_isr.c` + `task_radio_isr.h` (replaced by `task_radio_combined.c`)
- [ ] `Application/Tasks/task_radio_processing.c` + `task_radio_processing.h` (same)
- [ ] `Application/Tasks/task_ethernet_rx.c` + `task_ethernet_rx.h` (replaced by `task_ethernet.c`)
- [ ] `Application/Tasks/task_ethernet_tx.c` + `task_ethernet_tx.h` (same)
- [ ] `Application/Tasks/task_networkmgmt.c` + `task_networkmgmt.h` (replaced by `task_network_mgmt.c`)
- [ ] `Application/Tasks/task_netmgmt_telnet.c` + `task_netmgmt_telnet.h` (not wired)

Also:  
- [ ] `Application/Tasks/task_network_mgmt.c` — stub poll bodies need real logic or delegation to `task_dhcp_arp.c` / `task_snmp.c` entry points

---

### 🟡 TODO-15: Fix TX preparation trigger in radio processing

**File**: `Application/Tasks/task_radio_processing.c:177`

When a radio TX frame is ready for uplink, the radio task must notify the TDMA task.

- [ ] Replace TODO comment with `xQueueSend(xRadioTxQueue, &pkt, 0)` or equivalent
- [ ] Ensure TDMA task dequeues and writes to SI4463 TX FIFO at the scheduled slot

---

### 🟡 TODO-16: Forward signaling/TDMA frames from radio processing

**File**: `Application/Tasks/task_radio_processing.c:371`, `383`

Received signaling frames (protocol 0x1E) and TDMA allocation frames (0x1F) are
recognized but not forwarded to the respective tasks.

- [ ] `protocol == 0x1E`: send frame data to signaling task via queue or call handler directly
- [ ] `protocol == 0x1F`: send frame data to TDMA task via queue or call `TDMA_slave_alloc_exploitation()` directly

---

## Priority 6 — Advanced Features (post-MVP)

### 🔵 TODO-17: Power management between TDMA slots

- [ ] Use WFI/WFE or `__WFI()` in idle task hook between TDMA slot boundaries
- [ ] Configure STM32 low-power STOP mode (requires careful clock restart)
- [ ] Measure power consumption improvement

---

### 🔵 TODO-18: Extended diagnostics / log ring buffer

- [ ] Implement circular log buffer in external SRAM (e.g., 8 KB)
- [ ] Add `LOG(level, msg)` macro that writes timestamped entry to buffer
- [ ] Expose last N log entries via new `show log` CLI command and SNMP trap

---

### 🔵 TODO-19: Firmware update mechanism

- [ ] Design flash sector layout for A/B firmware images
- [ ] Implement Xmodem or YMODEM receive over UART (serial CLI)
- [ ] Implement CRC verify + swap active image + reboot

---

## Dependency Graph

```
TODO-1 (FEC)
  └──► TODO-3 (signaling TX)
  └──► TODO-7 (IPv4→radio routing)

TODO-2 (parity)
  └──► TODO-4 (TDMA master alloc)
  └──► TODO-5 (TDMA slave alloc)

TODO-4 + TODO-5 + TODO-6 (TDMA)
  └──► TODO-3 (signaling TX slot scheduling)
  └──► TODO-15 (TX trigger)

TODO-7 + TODO-8 (routing + ARP)
  └──► End-to-end IP traffic (system test)

TODO-10 + TODO-11 + TODO-12 (DHCP/ARP socket I/O)
  └──► DHCP server functional test
```

---

## Suggested Work Order

1. **TODO-1** (FEC codec) — all radio traffic blocked without this  
2. **TODO-2** (parity bits) — required for correct TDMA byte framing  
3. **TODO-3** (signaling TX FIFO) — enables signaling frames to be sent  
4. **TODO-4 + 5 + 6** (TDMA alloc + null frame) — enables multi-client TDMA  
5. **TODO-10 + 11 + 12** (DHCP socket I/O) — enables IP address assignment  
6. **TODO-7 + 8** (IPv4 routing + ARP) — enables end-to-end data traffic  
7. **TODO-13** (monitor task) — enable temperature recalibration  
8. **TODO-14 + 15 + 16** (hygiene) — clean up redundant files  
9. **TODO-9** (FDD downlink) — optional, enables FDD mode  
10. **TODO-17/18/19** (advanced features) — post-MVP  
