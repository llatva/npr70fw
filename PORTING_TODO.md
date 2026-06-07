# NPR-70 FreeRTOS Port — Remaining TODO List

**Status**: 🟢 All critical protocol implementation TODOs complete!

---

### ✅ TODO-9: Implement FDD downlink packet handling — COMPLETE (2026-06-07)

**File edited**: `Application/Tasks/task_ethernet.c`  
**Reference**: `source/Eth_IPv4.cpp` — FDD downlink path (UDP port 6716 / `FDD_DOWN_PORT`)

**Implementation**:
- ✅ Detect UDP packets to modem's IP on port 6716 in master FDD mode
- ✅ Extract UDP payload containing raw radio packet data
- ✅ Inject payload into RX FIFO via `RX_FIFO_Write()`
- ✅ Notify radio task via `xRadioISRQueue` to process injected packet
- ✅ Added `InjectFDDDownlink()` function with proper error checking

**FDD Operation**: Allows a master modem in FDD (Frequency Division Duplex) mode to receive
downlink packets via Ethernet from another modem that's receiving them on a different frequency.
The UDP payload contains a raw radio packet that is injected into the RX path as if received
from the SI4463 radio.

**Build Impact**: +288 bytes Flash (69,724 total, 26.6%)

---

## Remaining Work Items (Advanced Features)

These are enhancements beyond the core protocol implementation:
