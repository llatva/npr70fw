# NPR-70 FreeRTOS Port - Quick Start Guide

## What Has Been Done ✅

I've completed a comprehensive analysis and planning phase for porting your NPR-70 modem firmware from mbed OS to FreeRTOS 11.x LTS with STM32 HAL drivers.

### Created Files

1. **Core Configuration**
   - `Core/Inc/FreeRTOSConfig.h` - FreeRTOS configuration for STM32L432KC
   - `Core/Inc/main.h` - Main system header with pin definitions
   - `Core/Inc/npr_types.h` - Common types, structures, queues

2. **Documentation** (4 comprehensive documents)
   - `README_FREERTOS.md` - Complete architecture guide (thread design, priorities, communication)
   - `IMPLEMENTATION_PLAN.md` - 85-hour detailed implementation plan with code examples
   - `PROJECT_STATUS.md` - Current status and progress tracking
   - `QUICKSTART.md` - This file

3. **Tools**
   - `download_dependencies.sh` - Automated script to download FreeRTOS and STM32 HAL

4. **Directory Structure**
   - Full project structure created (Core/, Application/, Drivers/, Middleware/)

### Key Design Decisions Made

✅ **Thread Architecture (9 tasks)**
```
Priority 7: Radio ISR Handler (time-critical TDMA)
Priority 6: Radio Processing
Priority 5: Ethernet RX
Priority 4: Ethernet TX
Priority 3: DHCP/ARP, SNMP
Priority 2: Telnet, Signaling
Priority 1: Monitor
Priority 0: Idle
```

✅ **Inter-Task Communication**
- Queues for packet flow (Radio↔Ethernet)
- Mutexes for SPI bus sharing (SI4463 on SPI1, W5500+SRAM on SPI2)
- Event groups for system state

✅ **Timing Strategy**
- TIM2: 32-bit @ 1MHz (microsecond timer for TDMA)
- TIM3: Runtime statistics
- SysTick: 1kHz for FreeRTOS

✅ **Memory Strategy**
- 32KB FreeRTOS heap (heap_4.c)
- External SRAM for overflow buffering
- Static allocation for critical paths

---

## Next Steps - What You Need to Do

### Step 1: Download Dependencies ⏭️ START HERE

Run the provided script to download FreeRTOS kernel and STM32 HAL:

```bash
cd /home/llatva/git/npr70fw
./download_dependencies.sh
```

This will create:
- `Middleware/FreeRTOS/` - FreeRTOS 11.1.0 LTS kernel
- `Drivers/STM32L4xx_HAL_Driver/` - STM32 HAL drivers
- `Drivers/CMSIS/` - CMSIS headers

**Time:** ~5-10 minutes (depends on internet speed)

---

### Step 2: Review the Architecture

Read the documentation to understand the design:

1. **Start with:** `README_FREERTOS.md`
   - Overview of architecture
   - Thread design and priorities
   - Task responsibilities
   - Queue and mutex usage

2. **Then read:** `IMPLEMENTATION_PLAN.md`
   - Detailed step-by-step plan
   - Code examples for each phase
   - 85-hour timeline estimate
   - Testing strategy

3. **Check:** `PROJECT_STATUS.md`
   - Current progress (foundation ~70% complete)
   - What's done vs. pending
   - Risk assessment

**Time:** ~2-3 hours to fully understand

---

### Step 3: Create Linker Script

Based on `BUILD/NPR_14.link_script.ld`, create `STM32L432KC.ld`:

**Key changes needed:**
- Reserve last 2KB of Flash for configuration storage
- Define FreeRTOS heap location in RAM
- Update for GCC ARM toolchain syntax

**See:** `IMPLEMENTATION_PLAN.md` Phase 1, Task 1.2

**Time:** ~1 hour

---

### Step 4: Implement System Initialization

Create these files in `Core/Src/`:

1. **main.c** - Entry point, HAL init, task creation
   - Clock configuration (80 MHz from MSI via PLL)
   - Peripheral initialization (SPI, UART, TIM, GPIO)
   - FreeRTOS object creation (queues, mutexes, event groups)
   - Task creation
   - Start scheduler

2. **system_stm32l4xx.c** - System clock configuration
   - Copy from CMSIS template
   - Configure for 80 MHz operation

3. **stm32l4xx_hal_msp.c** - HAL MSP callbacks
   - `HAL_SPI_MspInit()` - SPI pin configuration
   - `HAL_UART_MspInit()` - UART pin configuration
   - `HAL_TIM_Base_MspInit()` - Timer clock enable

4. **stm32l4xx_it.c** - Interrupt handlers
   - SI4463 EXTI interrupt
   - W5500 EXTI interrupt
   - FreeRTOS handlers (SysTick, PendSV, SVC)

**See:** `IMPLEMENTATION_PLAN.md` Phase 2

**Time:** ~6-8 hours

---

### Step 5: Create Build System

Choose one:

**Option A: Modernized Makefile** (Recommended - familiar)
- Auto-discover source files
- Proper dependency tracking
- Flash target via ST-Link

**Option B: CMake**
- More modern
- Better IDE integration
- Cross-platform

**See:** `IMPLEMENTATION_PLAN.md` Phase 1, Task 1.3 for templates

**Time:** ~2-3 hours

---

### Step 6: First Build and Test

Minimal test: LED blink via FreeRTOS task

```c
void vBlinkTask(void *pvParameters) {
    for (;;) {
        HAL_GPIO_TogglePin(LED_RX_PORT, LED_RX_PIN);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

**Success criteria:**
- Compiles without errors
- Links successfully
- Flashes to STM32L432KC
- LED blinks at 1Hz
- UART outputs "FreeRTOS started"

**Time:** ~2 hours (debugging build issues)

---

### Step 7: Port Hardware Drivers

1. **SI4463 radio driver**
   - Convert from mbed SPI to HAL SPI
   - Maintain exact timing for TDMA
   - Test: Read chip version

2. **W5500 Ethernet driver**
   - Convert from mbed SPI to HAL SPI
   - Mutex protection (shares SPI2)
   - Test: Ping response

3. **External SRAM driver**
   - Convert from mbed SPI to HAL SPI
   - Mutex protection (shares SPI2)
   - Test: Read/write verify

**See:** `IMPLEMENTATION_PLAN.md` Phase 3

**Time:** ~10-14 hours

---

### Step 8: Implement Tasks (Core Functionality)

Follow the order in `IMPLEMENTATION_PLAN.md`:

1. **Phase 4:** Radio tasks (12-16 hours)
   - Radio ISR handler task
   - TDMA protocol
   - L1/L2 radio protocol

2. **Phase 5:** Ethernet tasks (8-10 hours)
   - Ethernet RX task
   - Ethernet TX task
   - IPv4 packet handling

3. **Phase 6:** Service tasks (8-10 hours)
   - DHCP/ARP service
   - SNMP agent
   - Telnet HMI
   - Signaling
   - System monitor

**Time:** ~30-36 hours total

---

### Step 9: Integration Testing

Follow the test plan in `README_FREERTOS.md`:

1. **Unit tests** - Each driver in isolation
2. **Integration tests** - Subsystems together
3. **System tests** - End-to-end scenarios
4. **Performance tests** - Timing, throughput, stability

**Success criteria:**
- TDMA master-slave link working
- IP packets routed through radio
- All services operational
- 24-hour stability test passed

**Time:** ~10-15 hours

---

## Total Time Estimate

| Phase | Description | Hours |
|-------|-------------|-------|
| Planning | ✅ DONE | 6 |
| Dependencies | Download | 0.5 |
| Foundation | Linker, init, build | 10 |
| First Test | Blink LED | 2 |
| Drivers | SI4463, W5500, SRAM | 14 |
| Radio Tasks | ISR, TDMA, L1/L2 | 16 |
| Ethernet Tasks | RX, TX, IPv4 | 10 |
| Services | DHCP, SNMP, Telnet | 10 |
| Testing | Validation | 15 |
| **TOTAL** | | **~84 hours** |

**Realistic timeline:** 3-4 weeks with 4-5 hours per day

---

## Important Notes

### Critical Success Factors

1. **TDMA Timing is Sacred**
   - Use logic analyzer to verify timing
   - Compare with original mbed version
   - Microsecond precision required

2. **Mutex Discipline**
   - Always lock SPI mutexes before access
   - Keep critical sections short (<500µs)
   - Priority inheritance configured

3. **Stack Monitoring**
   - Check high water marks regularly
   - Tune sizes based on actual usage
   - Enable overflow detection

4. **Incremental Testing**
   - Test each component before integrating
   - Don't skip unit tests
   - Verify timing at each stage

### Common Pitfalls to Avoid

❌ **Don't:**
- Skip the architecture review (read the docs!)
- Guess at stack sizes (monitor and tune)
- Mix mbed and HAL APIs
- Access shared SPI without mutex
- Assume timing is preserved (verify with scope)

✅ **Do:**
- Follow the implementation plan phase by phase
- Test incrementally after each step
- Use FreeRTOS runtime stats to monitor performance
- Keep ISR duration minimal (<50µs)
- Document any deviations from the plan

---

## Questions & Troubleshooting

### Q: Download script fails?
**A:** Manual download:
- FreeRTOS: https://github.com/FreeRTOS/FreeRTOS-Kernel/releases/tag/V11.1.0
- STM32CubeL4: https://github.com/STMicroelectronics/STM32CubeL4

### Q: Build errors with HAL?
**A:** Check:
- `STM32L432xx` defined in compiler flags
- `USE_HAL_DRIVER` defined
- Include paths correct for CMSIS and HAL
- Linker script has correct memory sizes

### Q: FreeRTOS won't start?
**A:** Check:
- `vTaskStartScheduler()` called
- At least one task created
- Sufficient heap configured (32KB)
- SysTick interrupt enabled
- Vector table handlers correct

### Q: TDMA timing off?
**A:** Verify:
- TIM2 prescaler correct (80MHz/80 = 1MHz)
- `HAL_GetUsTick()` returns increasing values
- Use oscilloscope to measure actual timing
- Compare with original mbed timing

### Q: Task stack overflow?
**A:** Check:
- Stack overflow detection enabled (Method 2)
- Monitor high water marks
- Increase stack size for affected task
- Check for recursive calls or large local arrays

---

## Resources

### Quick Reference
- **Architecture:** `README_FREERTOS.md`
- **Implementation:** `IMPLEMENTATION_PLAN.md`
- **Status:** `PROJECT_STATUS.md`

### External Documentation
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [STM32L4 Reference Manual RM0394](https://www.st.com/resource/en/reference_manual/rm0394-stm32l41xxx42xxx43xxx44xxx45xxx46xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [STM32L432KC Datasheet](https://www.st.com/resource/en/datasheet/stm32l432kc.pdf)

### Tools
- **Compiler:** arm-none-eabi-gcc (install: `apt-get install gcc-arm-none-eabi`)
- **Flasher:** st-flash (install: `apt-get install stlink-tools`)
- **Debug:** gdb-multiarch with OpenOCD or ST-Link GDB server

---

## Summary

✅ **What's Ready:**
- Complete architecture design
- FreeRTOS configuration
- Type definitions
- Comprehensive documentation
- Directory structure
- Download script

⏭️ **Next Action:**
```bash
./download_dependencies.sh
```

📖 **Then Read:**
1. `README_FREERTOS.md` (architecture overview)
2. `IMPLEMENTATION_PLAN.md` (step-by-step guide)

⚡ **Remember:**
- Test incrementally
- Verify timing with oscilloscope
- Monitor stack usage
- Follow the plan!

**Good luck with the implementation! The foundation is solid, now it's time to build.**

---

**Questions?** Refer to the detailed documentation or the original NPR project at:
https://hackaday.io/project/164092-npr-new-packet-radio

**Created:** November 7, 2025
**Author:** llatva (FreeRTOS port) / F4HDK (original NPR firmware)
