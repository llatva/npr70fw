# NPR-70 FreeRTOS Port — Remaining TODO List

**Date**: June 7, 2026  
**Status**: Core bidirectional data path COMPLETE — Radio↔IPv4 routing functional  
**Blocking full hardware test**: items marked 🔴 (critical)

Reference originals are in `source/` (mbed C++ code).  
All new C implementations go in `Application/Tasks/` or `Application/Services/`.


### ✅ TODO-4a: SI4463 driver things — COMPLETE (2025-01-14)
- ⚠️ **PARTIAL**: TX complete event / callback still needs integration with signaling task
- ⚠️ **PARTIAL**: FIFO space check before write can be added as enhancement

**Implementation Details**:
- Created `SI4463_PrepareTX()` function that switches to TX_TUNE state, resets FIFOs, sets preamble length
- Created `SI4463_TxToRxTransition()` for TX→RX state transitions
- Added state definitions to support TX preparation workflow

---

### 🟠 TODO-9: Implement FDD downlink packet handling

**File to edit**: `Application/Tasks/task_ethernet.c`  
**Reference**: `source/Eth_IPv4.cpp` — FDD downlink path (UDP port 6716 / `FDD_DOWN_PORT`)

- [ ] On receipt of UDP dst port 6716, extract payload and inject into radio RX path
- [ ] Fill TODO at `task_ethernet_rx.c:185` (or equivalent in `task_ethernet.c`)

---

## Priority 6 — Advanced Features (post-MVP)

### 🔵 TODO-19: Firmware update mechanism

- [ ] Implement Xmodem or YMODEM receive over UART (serial CLI)
- [ ] Implement CRC verify + swap active image + reboot

---

## Suggested Work Order

9. **TODO-9** (FDD downlink) — optional, enables FDD mode  
10. **TODO-17/18/19** (advanced features) — post-MVP  
