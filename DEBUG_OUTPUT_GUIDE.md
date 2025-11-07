# Debug Output Guide - NPR-70 FreeRTOS

## What Was Added

### 1. UART Printf Retargeting (`Core/Src/syscalls.c`)
- Redirects `printf()` to UART2 (921600 baud, 8N1)
- Uses HAL_UART_Transmit for blocking output
- Connects to newlib's `_write()` system call
- **Same baud rate as original NPR-70 mbed firmware**

### 2. Boot Debug Messages (`Core/Src/main.c`)
The firmware now prints detailed boot progress:

```
NPR-70, FreeRTOS FW v1.0
Build: Nov 7 2025 23:45:00
Serial: 921600 baud, 8N1
=================================
Boot: HAL Init OK
Boot: TIM2 started
Boot: Watchdog initialized
Boot: Global variables initialized
Boot: Mutexes created
Boot: Queues created
Boot: Event groups created
Boot: Config flash initialized
Boot: Config loaded from flash
Boot: Initializing W5500...
Boot: W5500 OK
Boot: W5500 sockets configured
Boot: Initializing SI4463...
Boot: SI4463 OK
Boot: Checking for external SRAM...
Boot: External SRAM detected and initialized
Boot: Initializing task modules...
Boot: Task modules initialized
Boot: Creating FreeRTOS tasks...
Boot: All tasks created
Boot: Starting FreeRTOS scheduler...
```

## UART2 Connection

**STM32L432KC Pinout:**
- **TX (PA2)** - Pin 7 on Arduino header (A7/D1)
- **RX (PA3)** - Pin 8 on Arduino header (A2/D0)
- **Settings**: 921600 baud, 8 data bits, No parity, 1 stop bit (8N1)
- **Note**: Same baud rate as original NPR-70 mbed firmware

## Common Issues and Solutions

### Issue 1: No Output at All
**Symptoms:** Nothing on serial terminal

**Possible Causes:**
1. **Wrong baud rate** - Must be 921600 (not 115200!)
2. **Wrong pins** - Check PA2 (TX) and PA3 (RX)
3. **Inverted TX/RX** - USB-UART RX connects to board TX
4. **Hardware stuck before UART init** - SystemClock or HAL_Init failure

**Debug:**
- Check LED_RX (should NOT be blinking fast = no Error_Handler)
- Verify USB-UART adapter is working (loopback test)
- Use oscilloscope on PA2 to see if data is being transmitted

### Issue 2: Output Stops After Specific Message
**Symptoms:** Boot messages print, then stops mid-sequence

**If stops at:**
- `"Boot: Initializing W5500..."` → W5500 not responding (SPI3 or CS pin issue)
- `"Boot: Initializing SI4463..."` → SI4463 not responding (SPI1 or CS pin issue)
- `"Boot: Checking for external SRAM..."` → SRAM check hangs (non-critical)
- `"Boot: Starting FreeRTOS scheduler..."` → Scheduler failed (heap overflow, stack issue)

**Actions:**
- Last message indicates where system got stuck
- Check corresponding hardware (W5500/SI4463/SRAM)
- Verify SPI connections and chip select pins
- Check power supply (3.3V stable)

### Issue 3: Garbage Characters
**Symptoms:** Random/unreadable characters on terminal

**Causes:**
- Incorrect baud rate setting
- Corrupted clock configuration
- Ground not connected between board and USB-UART

**Fix:**
- Set terminal to 921600 8N1
- Verify GND connection
- Check SystemClock_Config() in code

### Issue 4: LED Blinking Fast
**Symptoms:** LED_RX blinking rapidly, no serial output

**Cause:** Error_Handler() was triggered during initialization

**Indicates:**
- HAL peripheral initialization failed
- SPI init error
- GPIO init error
- One of the `if (...!= HAL_OK) Error_Handler()` checks failed

**Debug:**
- Count blink pattern to identify which init failed
- Check hardware connections
- Verify 3.3V power is stable

### Issue 5: Partial Boot Messages, Then Hangs
**Symptoms:** Some messages print, system appears to stop

**If last message was:**
- Before "Mutexes created" → InitializeGlobalVariables() hangs
- Before "W5500 OK" → W5500_Init() timeout (SPI communication issue)
- Before "SI4463 OK" → SI4463_Init() timeout or config failure
- After "Starting FreeRTOS scheduler..." → Scheduler started, but a task crashed

**Actions:**
- If scheduler started: Check for stack overflow, heap exhausted, or task assert
- If before scheduler: Hardware initialization timeout (increase timeout values)

## Testing Serial Output

### Test 1: Basic Connection Test
```bash
# Linux/macOS
screen /dev/ttyUSB0 921600

# Or use minicom
minicom -D /dev/ttyUSB0 -b 921600

# Windows - PuTTY
# COM port, 921600 baud, 8N1
```

### Test 2: Watch Boot Sequence
1. Connect serial terminal
2. Reset board (press RESET button or power cycle)
3. Should see full boot sequence within 1-2 seconds

### Test 3: Verify UART Hardware
```bash
# Loopback test (disconnect from board first!)
# Connect TX to RX on USB-UART adapter
# Type in terminal - should see characters echoed back
```

## Expected Behavior

**Successful Boot:**
1. All boot messages appear in ~1-2 seconds
2. Last message: "Starting FreeRTOS scheduler..."
3. No LED blinking (error condition)
4. System continues running (watchdog refreshing)

**After Boot:**
- Tasks are running
- Watchdog refreshes every 1 second
- Telnet should be accessible (if W5500 connected)
- Radio tasks responding to interrupts

## Memory Status
- Flash: 57 KB / 256 KB (22.3%)
- RAM: ~64 KB / 65.5 KB (97.6%)
- FreeRTOS heap: 12 KB
- Task stacks: Reduced to minimum safe sizes

## Debugging Steps

1. **Connect serial terminal** at 921600 baud, 8N1
2. **Reset board** or power cycle
3. **Observe output:**
   - If NOTHING: Check UART connections, baud rate
   - If PARTIAL: Note last message, indicates where it failed
   - If LED BLINKING: Error_Handler triggered, check hardware
   - If FULL BOOT: System initialized successfully!

4. **If no output and LED not blinking:**
   - System may be stuck before UART init
   - Check power supply (3.3V)
   - Try erasing flash: `st-flash erase`
   - Reflash firmware: `make flash`

## Additional Debug Options

If serial output still doesn't help, consider:
1. Use debugger (ST-Link with GDB)
2. Add LED toggle in main loop before scheduler
3. Use oscilloscope on PA2 to confirm UART TX activity
4. Check for shorts on SPI buses (could hang init)

## Quick Reference Commands

```bash
# Build with debug output
make clean && make

# Flash to board  
make flash

# Monitor serial output (921600 baud)
screen /dev/ttyUSB0 921600

# Erase flash
st-flash erase

# Check binary size
arm-none-eabi-size build/NPR70_FreeRTOS.elf
```

## Version Info
- Firmware: NPR-70 FreeRTOS v1.0
- Build includes: Watchdog, Flash Config, UART Debug Output
- Last updated: November 7, 2025
