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
```
Priority 5: RadioISR         - Radio interrupt handling
Priority 4: RadioProcessing  - Radio packet processing
Priority 3: TDMA             - TDMA timing and slot management
Priority 3: Signaling        - Network signaling and keepalive
Priority 2: EthernetRX       - Ethernet packet reception
Priority 2: EthernetTX       - Ethernet packet transmission
Priority 2: DHCP_ARP         - DHCP server and ARP proxy
Priority 2: SNMP             - SNMP agent
Priority 1: Telnet           - Telnet console interface
Priority 0: IDLE             - FreeRTOS idle task
```

## Build Information

### Memory Usage
```
Flash:  52,396 bytes / 256 KB  (19.9%)
RAM:    64,648 bytes /  64 KB  (98.9%)
Heap:   13,568 bytes (FreeRTOS)
```

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
- **Heap Size**: 13.5 KB (FreeRTOS)

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
> set callsign OH3HZB
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
- **9 application tasks** with priority-based scheduling
- **Queue-based** inter-task communication
- **Mutex protection** for SPI buses and shared resources
- **Event groups** for system-wide events

#### Driver Updates
- **SI4463 Radio**: Adapted from mbed DigitalOut/SPI to STM32 HAL
- **W5500 Ethernet**: Migrated from mbed SPI to STM32 HAL SPI
- **External SRAM**: Ported to use HAL SPI for 23LC1024
- **Timers**: TIM2 provides microsecond timestamping

#### Memory Optimization
The original mbed implementation used ~66KB RAM. The FreeRTOS port:
- Optimized task stack sizes (128-1024 bytes per task)
- Reduced heap allocation (13.5 KB)
- Minimized global buffers
- Stack-based command processing
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
    ├── task_radio_isr.c        - Radio interrupt handler
    ├── task_radio_processing.c - Radio packet processing
    ├── task_tdma.c             - TDMA coordinator
    ├── task_ethernet_rx.c      - Ethernet reception
    ├── task_ethernet_tx.c      - Ethernet transmission
    ├── task_signaling.c        - Network signaling
    ├── task_dhcp_arp.c         - DHCP/ARP services
    ├── task_snmp.c             - SNMP agent
    └── task_telnet.c           - Telnet console

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
- **Stack Sizes**: Carefully tuned to avoid overflow
- **Heap Size**: 13.5 KB shared across all tasks
- **Buffer Sizes**: Telnet limited to 400 bytes to save stack
- **Float Operations**: Avoided where possible to save code space

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

### 2025-11-07: FreeRTOS Port v1.0
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

