# NPR-70 FreeRTOS Port - Project Status

## Overview

This document tracks the status of the NPR-70 modem firmware port from mbed OS to FreeRTOS 11.x LTS with STM32 HAL drivers.

**Project Started:** November 7, 2025
**Target:** STM32L432KC (Cortex-M4F @ 80MHz)
**RTOS:** FreeRTOS 11.1.0 LTS
**HAL:** STM32 HAL/LL drivers (no mbed)

---

## Current Status: Dependencies Installed, Building Foundation ✅

### Completed Items

#### Phase 0: Dependencies (100% Complete)

- [x] **FreeRTOS Kernel 11.1.0 LTS**
  - Downloaded and installed in `Middleware/FreeRTOS/`
  - ARM Cortex-M4F port verified
  - heap_4 memory allocator present

- [x] **STM32 HAL Drivers**
  - Downloaded STM32L4xx HAL Driver in `Drivers/STM32L4xx_HAL_Driver/`
  - Headers and sources verified
  - Submodule properly initialized

- [x] **CMSIS**
  - CMSIS Core headers installed (`Drivers/CMSIS/Include/`)
  - STM32L432xx device headers installed
  - Cortex-M4 core definitions present

#### Phase 1: Planning and Architecture (100% Complete)

- [x] **Requirements Analysis**
  - Analyzed mbed-based codebase
  - Identified timing-critical sections (TDMA, SI4463 ISR)
  - Mapped out thread architecture
  - Defined synchronization primitives

- [x] **System Architecture Design**
  - 9 tasks with priority-based scheduling
  - Inter-task communication via queues
  - Mutex-protected shared resources (SPI buses)
  - Event groups for system coordination

- [x] **FreeRTOS Configuration**
  - Created `Core/Inc/FreeRTOSConfig.h`
  - Configured for STM32L432KC Cortex-M4F
  - Optimized for real-time TDMA timing
  - 32KB heap, heap_4 allocator
  - Stack overflow detection enabled
  - Runtime statistics configured

- [x] **Type Definitions and Structures**
  - Created `Core/Inc/npr_types.h`
  - Defined packet structures (radio, ethernet, ISR events)
  - Hardware context structures (SI4463, W5500, SRAM)
  - TDMA configuration structures
  - System configuration structures

- [x] **Main Configuration Header**
  - Created `Core/Inc/main.h`
  - Pin definitions for STM32L432KC
  - Peripheral handles declarations
  - FreeRTOS object declarations
  - Exported function prototypes

- [x] **Project Directory Structure**
  ```
  npr70fw/
  ├── Core/Inc/             ✅ Created
  ├── Core/Src/             ✅ Created
  ├── Application/Radio/    ✅ Created
  ├── Application/Ethernet/ ✅ Created
  ├── Application/Services/ ✅ Created
  ├── Application/Drivers/  ✅ Created
  ├── Drivers/              ✅ Created (for HAL)
  └── Middleware/FreeRTOS/  ✅ Created (for kernel)
  ```

- [x] **Documentation**
  - `README_FREERTOS.md` - Comprehensive architecture guide
  - `IMPLEMENTATION_PLAN.md` - Detailed 79-hour implementation plan
  - `PROJECT_STATUS.md` - This file
  - Inline code documentation standards defined

- [x] **Build Preparation**
  - `download_dependencies.sh` script created
  - Makefile template designed (pending implementation)
  - CMake option considered
  - Linker script requirements documented

---

## Next Steps: Implementation Phase

### Immediate (Next Session)

1. **Download Dependencies**
   ```bash
   ./download_dependencies.sh
   ```
   - FreeRTOS 11.1.0 kernel
   - STM32CubeL4 HAL drivers

2. **Create Linker Script**
   - File: `STM32L432KC.ld`
   - Based on existing `BUILD/NPR_14.link_script.ld`
   - Reserve 2KB flash for configuration
   - Place FreeRTOS heap in RAM

3. **Implement System Initialization**
   - File: `Core/Src/system_stm32l4xx.c`
   - System clock: 80 MHz from MSI via PLL
   - Vector table setup

4. **Implement Startup Code**
   - File: `startup_stm32l432xx.s`
   - Vector table with FreeRTOS handlers
   - Early initialization

5. **Create Build System**
   - File: `Makefile` (modernized version)
   - OR: `CMakeLists.txt` (if preferred)
   - Auto-discover source files
   - Proper dependency tracking

### Short Term (Week 1-2)

6. **Implement Core HAL Initialization**
   - `Core/Src/main.c` - Main entry point
   - `Core/Src/stm32l4xx_hal_msp.c` - HAL callbacks
   - `Core/Src/stm32l4xx_it.c` - Interrupt handlers
   - Clock, GPIO, SPI, UART, Timer initialization

7. **Port Hardware Drivers**
   - SI4463 radio driver
   - W5500 Ethernet driver
   - External SRAM driver
   - Configuration flash storage

8. **Test Basic System**
   - FreeRTOS scheduler running
   - LED blink task
   - UART output
   - SPI communication test

### Medium Term (Week 2-3)

9. **Implement Radio Tasks**
   - Radio ISR handler task (priority 7)
   - Radio processing task (priority 6)
   - TDMA protocol
   - L1/L2 radio protocol

10. **Implement Ethernet Tasks**
    - Ethernet RX task (priority 5)
    - Ethernet TX task (priority 4)
    - IPv4 packet handling
    - DHCP/ARP service (priority 3)

11. **Implement Service Tasks**
    - SNMP agent (priority 3)
    - Telnet HMI (priority 2)
    - Signaling task (priority 2)
    - Monitor task (priority 1)

### Long Term (Week 3-4)

12. **Integration Testing**
    - TDMA master/slave link
    - IP packet routing radio ↔ Ethernet
    - Multi-client TDMA
    - Full feature testing

13. **Optimization & Validation**
    - Stack usage profiling
    - CPU usage analysis
    - Memory leak detection
    - Timing accuracy verification
    - 24+ hour stability test

---

## Implementation Metrics

### Estimated Effort Breakdown

| Phase | Tasks | Est. Hours | Status |
|-------|-------|-----------|--------|
| Planning & Architecture | Design, docs | 6 | ✅ Complete |
| Foundation Setup | Downloads, build | 6 | ⏳ Next |
| Core System | main.c, HAL init | 8 | ⏳ Pending |
| Hardware Drivers | SI4463, W5500, SRAM | 14 | ⏳ Pending |
| Radio Tasks | TDMA, L1/L2 | 16 | ⏳ Pending |
| Ethernet Tasks | RX, TX, IPv4 | 10 | ⏳ Pending |
| Service Tasks | DHCP, SNMP, Telnet | 10 | ⏳ Pending |
| Testing & Optimization | Validation | 15 | ⏳ Pending |
| **Total** | | **85 hours** | **~7% done** |

### Progress: Foundation Phase

- **Planning:** 100% ✅
- **Architecture:** 100% ✅
- **Documentation:** 100% ✅
- **Directory Setup:** 100% ✅
- **Download Dependencies:** 0% ⏳ (script ready)
- **Build System:** 0% ⏳ (template designed)

**Overall Foundation Progress: ~70%**

---

## Critical Design Decisions

### 1. ✅ Thread Priority Assignment

```
Priority 7: Radio ISR Handler    (Time-critical TDMA)
Priority 6: Radio Processing     (FEC, packet routing)
Priority 5: Ethernet RX          (Network input)
Priority 4: Ethernet TX          (Network output)
Priority 3: DHCP/ARP, SNMP       (Network services)
Priority 2: Telnet, Signaling    (User interface)
Priority 1: Monitor              (Health checks)
Priority 0: Idle                 (Statistics, watchdog)
```

**Rationale:** TDMA requires microsecond precision, therefore highest priority for radio interrupt handling.

### 2. ✅ Deferred Interrupt Processing

**Pattern:** ISR → Queue → Task

```
Hardware IRQ → Minimal ISR (timestamp, queue event)
             → High-priority task (actual processing)
```

**Benefits:**
- Keeps ISR duration minimal (<50µs)
- Allows preemption during processing
- Maintains timing precision for TDMA

### 3. ✅ SPI Bus Sharing Strategy

**SPI1:** SI4463 radio (exclusive)
**SPI2:** W5500 Ethernet + External SRAM (shared)

**Protection:** Mutex with priority inheritance
**Critical sections:** Minimized (<500µs)

### 4. ✅ Memory Allocation Strategy

**FreeRTOS Heap:** 32KB (heap_4.c)
- Task stacks
- Queues
- Dynamic allocations

**Static Buffers:**
- Radio RX FIFO: 8KB (global)
- Packet queue elements: Static allocation for critical paths

**External SRAM:**
- Radio TX overflow buffering
- Large packet temporary storage

### 5. ✅ Timing Architecture

**TDMA Timer:** TIM2 (32-bit @ 1MHz = 1µs resolution)
**Runtime Stats:** TIM3 (for FreeRTOS statistics)
**SysTick:** 1kHz (FreeRTOS tick)

**API:**
```c
uint32_t HAL_GetUsTick(void);   // Microsecond timestamp
void delay_us(uint32_t us);     // Busy-wait microsecond delay
```

---

## Key Files Created

### Configuration & Headers
- ✅ `Core/Inc/FreeRTOSConfig.h` - FreeRTOS configuration
- ✅ `Core/Inc/main.h` - Main system header
- ✅ `Core/Inc/npr_types.h` - Common types and structures

### Documentation
- ✅ `README_FREERTOS.md` - Architecture and usage guide
- ✅ `IMPLEMENTATION_PLAN.md` - Detailed implementation steps
- ✅ `PROJECT_STATUS.md` - This status document

### Tools
- ✅ `download_dependencies.sh` - Dependency download script

### Directory Structure
- ✅ `Core/Inc/` and `Core/Src/`
- ✅ `Application/Radio/`, `Application/Ethernet/`, `Application/Services/`, `Application/Drivers/`
- ✅ `Drivers/` (for STM32 HAL)
- ✅ `Middleware/FreeRTOS/` (for kernel)

---

## Technical Challenges Identified

### 1. TDMA Timing Precision ⚠️

**Requirement:** Microsecond-accurate timing for slot synchronization
**Solution:** 
- Dedicated TIM2 for µs timestamps
- High-priority Radio ISR task (priority 7)
- Critical sections for timing-sensitive code
- Verification with logic analyzer

**Risk Level:** HIGH - Critical for protocol operation

### 2. SPI Bus Contention ⚠️

**Issue:** W5500 and SRAM share SPI2
**Solution:**
- Mutex protection with priority inheritance
- Minimize transaction duration
- Monitor mutex wait times

**Risk Level:** MEDIUM - May impact throughput

### 3. Stack Sizing 🔧

**Challenge:** Limited 64KB RAM, 9 tasks
**Solution:**
- Conservative initial sizing (total ~12KB)
- Runtime high water mark monitoring
- Tune based on profiling

**Risk Level:** LOW - Can adjust during testing

### 4. Real-time vs. Throughput Trade-off 🔧

**Challenge:** High-priority radio task may starve lower-priority tasks
**Solution:**
- Careful priority assignment
- Monitor CPU usage per task
- Limit Radio task blocking time

**Risk Level:** MEDIUM - Requires tuning

---

## Testing Strategy

### Unit Testing (Per Component)
- [ ] Clock configuration (verify 80MHz with oscilloscope)
- [ ] GPIO toggling (verify pin states)
- [ ] SPI loopback (verify communication)
- [ ] UART echo (verify serial communication)
- [ ] Timer accuracy (verify µs timing with scope)

### Integration Testing (Per Subsystem)
- [ ] FreeRTOS scheduler (multiple tasks, priorities)
- [ ] SI4463 initialization (read version, configure)
- [ ] W5500 initialization (PHY link, ping response)
- [ ] TDMA timing (verify slot boundaries with scope)
- [ ] Packet flow (radio RX → ethernet TX)

### System Testing (End-to-End)
- [ ] Two modems: TDMA master-slave link
- [ ] IP ping through radio link
- [ ] Multi-client TDMA allocation
- [ ] DHCP server assigning IPs
- [ ] Telnet configuration session
- [ ] SNMP queries
- [ ] 24-hour stability test

### Performance Testing
- [ ] TDMA timing jitter (<50µs acceptable)
- [ ] Packet throughput (compare with mbed version)
- [ ] CPU usage per task (<80% total)
- [ ] Stack usage (<80% per task)
- [ ] Heap fragmentation (<20%)

---

## Risk Register

| Risk | Probability | Impact | Mitigation | Status |
|------|------------|--------|------------|--------|
| TDMA timing drift | Medium | Critical | Use dedicated TIM2, verify with scope | ✅ Planned |
| SPI contention | Low | High | Mutex + priority inheritance | ✅ Planned |
| Stack overflow | Low | High | Conservative sizing + detection | ✅ Planned |
| Memory fragmentation | Low | Medium | heap_4 + static buffers | ✅ Planned |
| Interrupt latency | Medium | High | Minimize ISR duration | ✅ Planned |
| HAL driver bugs | Low | Medium | Use latest STM32Cube, test thoroughly | ⏳ Pending |
| Build system issues | Low | Low | Template from existing Makefile | ⏳ Pending |

---

## Success Criteria

### Minimum Viable Product (MVP)
- [ ] Compiles and links successfully
- [ ] FreeRTOS scheduler running
- [ ] UART debug output working
- [ ] LED blink via task
- [ ] SI4463 version read successful
- [ ] W5500 ping response
- [ ] System stable for 1 hour

### Feature Parity with mbed Version
- [ ] TDMA master mode operational
- [ ] TDMA slave mode operational
- [ ] Multi-client TDMA allocation
- [ ] IP packet routing radio ↔ Ethernet
- [ ] DHCP server
- [ ] Telnet CLI (all commands)
- [ ] SNMP agent
- [ ] Signaling protocol
- [ ] Configuration flash storage
- [ ] Temperature recalibration

### Production Ready
- [ ] All features tested and working
- [ ] 24+ hour stability test passed
- [ ] No memory leaks detected
- [ ] Stack usage <80% on all tasks
- [ ] CPU usage <80% average
- [ ] TDMA timing jitter <50µs
- [ ] Documentation complete
- [ ] Build system automated

---

## Lessons Learned (Ongoing)

### From mbed Analysis
1. **Timing is critical:** TDMA uses microsecond precision, cannot tolerate jitter
2. **SPI sharing:** Original code had potential race conditions, FreeRTOS mutexes will improve this
3. **Interrupt handling:** mbed's InterruptIn was simple but inflexible, FreeRTOS deferred processing is more powerful
4. **Memory management:** Original used large global buffers, FreeRTOS queues are cleaner

### Design Improvements over mbed
1. **Modular architecture:** Clear separation of concerns (radio, ethernet, services)
2. **Thread safety:** Explicit mutex protection vs. implicit assumptions
3. **Configurability:** FreeRTOS configuration is well-documented and tunable
4. **Debugging:** Runtime stats, stack monitoring, trace support
5. **Maintainability:** Standard RTOS patterns vs. custom polling loops

---

## Resources

### Documentation
- FreeRTOS Documentation: https://www.freertos.org/Documentation/RTOS_book.html
- STM32L4 Reference Manual: RM0394
- STM32L432KC Datasheet: DS11451
- SI4463 Datasheet: https://www.silabs.com/documents/public/data-sheets/Si4464-63-61-60.pdf
- W5500 Datasheet: https://www.wiznet.io/product-item/w5500/

### Tools
- ARM GCC Toolchain: https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm
- ST-Link Tools: https://github.com/stlink-org/stlink
- SEGGER SystemView (optional): https://www.segger.com/products/development-tools/systemview/

### Community
- FreeRTOS Forums: https://www.freertos.org/FreeRTOS_Support_Forum_Archive/freertos_support_forum_archive_index.html
- STM32 Community: https://community.st.com/
- NPR Project: https://hackaday.io/project/164092-npr-new-packet-radio

---

## Change Log

### 2025-11-07 - Foundation Complete
- Created project structure
- Designed architecture (9 tasks, priorities, queues)
- Configured FreeRTOS (FreeRTOSConfig.h)
- Defined types and structures (npr_types.h)
- Created comprehensive documentation
- Prepared download script for dependencies

**Status:** Foundation phase ~70% complete, ready to begin implementation

---

## Contact & Support

**Original NPR Firmware:** Guillaume F. F4HDK
**FreeRTOS Port:** Lasse OH3HZB

**GitHub Repository:** `npr70fw` branch `202511-freertos-port`

For questions or issues related to the FreeRTOS port, refer to:
- `README_FREERTOS.md` - Architecture guide
- `IMPLEMENTATION_PLAN.md` - Step-by-step instructions
- Original NPR project: https://hackaday.io/project/164092

---

**Last Updated:** November 7, 2025
**Next Review:** After dependency download completion
