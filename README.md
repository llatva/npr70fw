# NPR-70 Modem Firmware - FreeRTOS Port

This repository contains the NPR-70 modem firmware ported from mbed OS to FreeRTOS.

## Project Overview

**Original firmware** by F4HDK Guillaume (2017-2020)  
**FreeRTOS port** by OH3HZB Lasse (2025)

The NPR-70 is a high-speed amateur radio data modem operating in the 430-440 MHz band (70cm) or 144-148 MHz band (2m), supporting data rates from hundreds of kbps using GMSK modulation.

This port migrates the original mbed OS-based firmware to FreeRTOS 11.1.0 LTS, enabling use with STM32CubeMX and modern STM32 development tools while maintaining full functionality.

## Hardware Platform

- **MCU**: STM32L432KC (Cortex-M4F)
- **Flash**: 256 KB (19.9% used)
- **RAM**: 64 KB (98.9% used)
- **Radio**: Silicon Labs SI4463 transceiver
- **Ethernet**: WIZnet W5500 controller
- **External RAM**: Optional SPI SRAM (23LC1024)

## Features

### Core Functionality
- ✅ **TDMA Protocol**: Time Division Multiple Access for coordinated radio communication
- ✅ **Master/Client Modes**: Flexible network topology
- ✅ **Radio Control**: Complete SI4463 driver with TX/RX management
- ✅ **Ethernet Bridge**: W5500 Ethernet controller with full IP stack

### Network Services
- ✅ **DHCP Server**: Dynamic IP allocation for radio clients
- ✅ **ARP Proxy**: Address resolution for radio-side clients
- ✅ **SNMP Agent**: Network management (UDP port 161) with NPR-70 MIB
- ✅ **Telnet Console**: Full CLI on TCP port 23 (see TELNET_COMMANDS.md)

### FreeRTOS Task Architecture
The firmware runs 7 application tasks plus system tasks:

```
Priority 7: Radio            - Combined ISR handling and packet processing (240 bytes stack)
Priority 6: TDMA             - TDMA timing and slot management (160 bytes)
Priority 5: Signaling        - Network signaling and keepalive (128 bytes)
Priority 4: Ethernet         - Combined RX/TX packet handling (200 bytes)
Priority 3: NetMgmt          - Combined DHCP/ARP + SNMP services (160 bytes)
Priority 2: Telnet           - Telnet console interface (160 bytes)
Priority 1: Watchdog         - Task monitoring and hardware watchdog refresh (128 bytes)
Priority 0: IDLE             - FreeRTOS idle task (configMINIMAL_STACK_SIZE)
```

**Task Consolidation**: Original design had 9 separate tasks. Current implementation combines:
- RadioISR + RadioProcessing → **Radio** (saves 1 TCB + 1 stack)
- EthernetRX + EthernetTX → **Ethernet** (saves 1 TCB + 1 stack)
- DHCP_ARP + SNMP → **NetMgmt** (saves 1 TCB + 1 stack)

This consolidation reduces heap consumption and simplifies task management while preserving all functionality.

## Build Information

### Memory Usage
```
Flash:  53,064 bytes / 256 KB  (20.2%)
RAM:    64,648 bytes /  64 KB  (98.9%)
Heap:   18,432 bytes (FreeRTOS, 18 KB)
```
**Note**: RAM usage is at the limit. Heap has been increased to 18 KB to accommodate combined tasks and prevent malloc failures at boot.

### Toolchain
- **Compiler**: arm-none-eabi-gcc 13.2.1
- **Build System**: GNU Make
- **RTOS**: FreeRTOS 11.1.0 LTS
- **HAL**: STM32CubeL4 v1.18.0

### Build Commands
```bash
# Clean build
make clean

# Build firmware
make -j$(nproc)

# Flash (requires st-flash or compatible)
make flash
```

## Configuration

### Default Settings
- **Frequency**: 437.000 MHz (configurable 420-450 MHz)
- **Network ID**: 0 (configurable 0-15)
- **Modulation**: 22 (configurable 11-14, 20-24)
- **Mode**: Client (configurable via telnet)
- **Heap Size**: 18 KB (FreeRTOS - increased from 13.5 KB)

### Radio Bands
- **70cm band**: 420-450 MHz (default)
- **2m band**: 144-148 MHz (compile-time option)

## Usage

### Telnet Console
Connect via telnet to port 23 for configuration and monitoring:

```bash
telnet <modem-ip> 23
```

See `TELNET_COMMANDS.md` for complete command reference (18 commands available).

### Quick Start
```
> set callsign OH1CALL
> set network_id 5
> set frequency 437.000
> set is_master yes
> radio on
> show status
> save
```

## Port Details

### Migration from mbed OS to FreeRTOS

#### Task Structure
Original mbed OS used Ticker/Thread primitives. The FreeRTOS port implements:
- **7 application tasks** with priority-based scheduling (consolidated from original 9)
- **Queue-based** inter-task communication
- **Mutex protection** for SPI buses and shared resources
- **Event groups** for system-wide events
- **Task consolidation**: RadioISR+Processing, EthRX+TX, and DHCP+SNMP combined to reduce overhead

#### Driver Updates
- **SI4463 Radio**: Adapted from mbed DigitalOut/SPI to STM32 HAL
- **W5500 Ethernet**: Migrated from mbed SPI to STM32 HAL SPI
- **External SRAM**: Ported to use HAL SPI for 23LC1024
- **Timers**: TIM2 provides microsecond timestamping

#### Memory Optimization
The original mbed implementation used ~66KB RAM. The FreeRTOS port:
- Optimized task stack sizes (128-240 bytes per task)
- Reduced heap allocation (18 KB after optimization)
- Minimized global buffers
- Stack-based command processing
- **Task consolidation** to reduce TCB/stack overhead
- Achieved 98.9% RAM utilization without overflow

### Code Structure
```
Application/
├── Common/          - Shared definitions and global variables
│   ├── app_common.h/c
├── Ethernet/        - W5500 driver and network stack
│   ├── w5500_driver.h/c
├── Memory/          - External SRAM driver
│   ├── ext_sram_driver.h/c
├── Radio/           - SI4463 radio driver
│   ├── si4463_driver.h/c
└── Tasks/           - FreeRTOS task implementations
    ├── task_radio_combined.c   - Combined radio ISR + processing
    ├── task_tdma.c             - TDMA coordinator
    ├── task_ethernet.c         - Combined Ethernet RX + TX
    ├── task_signaling.c        - Network signaling
    ├── task_networkmgmt.c      - Combined DHCP/ARP + SNMP
    └── task_telnet.c           - Telnet console
    
    (Legacy task files remain but are filtered out in Makefile:
     task_radio_isr.c, task_radio_processing.c, 
     task_ethernet_rx.c, task_ethernet_tx.c,
     task_dhcp_arp.c, task_snmp.c)

Core/
├── Inc/             - STM32 HAL headers, main.h
└── Src/             - main.c, HAL initialization, startup code

Drivers/             - STM32 HAL and CMSIS
Middleware/          - FreeRTOS kernel
```

## Testing Status

### Verified Functions
- ✅ Build system (clean compilation)
- ✅ Task creation and scheduling
- ✅ Memory allocation (heap at limit)
- ✅ W5500 socket initialization
- ✅ Telnet CLI (18 commands)

### Requires Hardware Testing
- ⚠️ SI4463 radio TX/RX
- ⚠️ TDMA timing accuracy
- ⚠️ Ethernet packet flow
- ⚠️ DHCP client registration
- ⚠️ SNMP queries
- ⚠️ End-to-end radio bridge

## Known Limitations

1. **RAM Constraint**: At 98.9% utilization, no room for expansion
2. **Configuration Persistence**: Flash save/load not yet implemented
3. **Factory Reset**: Clears config but persistence not implemented
4. **External SRAM**: Detection implemented but not yet utilized
5. **Advanced Features**: Some original mbed features may need adaptation

## Development Notes

### Critical Constraints
- **Stack Sizes**: Carefully tuned to avoid overflow (128-240 bytes)
- **Heap Size**: 18 KB shared across all tasks (increased from initial 13.5 KB)
- **Buffer Sizes**: Telnet limited to 400 bytes to save stack
- **Float Operations**: Avoided where possible to save code space
- **Task Consolidation**: Required to fit within 64 KB RAM limit

### Future Enhancements
- Configuration save/restore to flash
- External SRAM utilization for packet buffers
- Watchdog timer implementation
- Power management optimization
- Extended diagnostics and logging

## License

This project is licensed under GPLv3.

Original NPR-70 firmware: Copyright (c) 2017-2020 Guillaume F. F4HDK  
FreeRTOS port: Copyright (c) 2025 Lasse OH3HZB

## References

- Original Project: https://hackaday.io/project/164092-npr-new-packet-radio
- SI4463 Datasheet: Silicon Labs
- W5500 Datasheet: WIZnet
- FreeRTOS: https://www.freertos.org/
- STM32L4 Series: STMicroelectronics

## Version History

### 2025-11-08: FreeRTOS Port v1.0 - Task Consolidation
- Task consolidation: 9 → 7 tasks (RadioISR+Processing, EthRX+TX, DHCP+SNMP combined)
- Heap increased to 18 KB to prevent boot malloc failures
- Combined radio task with 240-byte stack
- Memory optimized stack sizes across all tasks
- Build verified: 53,064 bytes flash, 64,648 bytes RAM

### 2025-11-07: FreeRTOS Port v1.0 - Initial
- Complete migration from mbed OS to FreeRTOS 11.1.0 LTS
- All 10 tasks implemented and building successfully
- W5500 socket configuration (DHCP, SNMP, Telnet)
- Full telnet CLI with 18 commands
- Memory optimized to 98.9% RAM utilization
- Build verified: 52,396 bytes flash, 64,648 bytes RAM

### 2020-05-16: Original mbed OS Release
- SNMP support added
- Settings refactor
- Memory usage optimization (original)
- Based on F4HDK's 2020_02_23 release

---

**Status**: Port complete, ready for hardware testing  
**Contact**: OH3HZB (FreeRTOS port), F4HDK (original firmware)

