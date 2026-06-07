# NPR-70 FreeRTOS Telnet Command Reference

## Connection
- **Port**: TCP 23
- **Protocol**: Telnet with echo and line editing
- **Timeout**: 5 minutes of inactivity
- **Prompt**: `ready>`

## Command Summary

### Information Commands

#### `help` or `?`
Display complete command reference.

#### `version`
Show firmware version information.
```
NPR-70 FreeRTOS Port
Firmware: 2025_11_07-freertos-v1.0
Build: Nov  7 2025 12:34:56
FreeRTOS: v11.1.0 LTS
```

#### `status`
Display comprehensive modem status.
```
Modem Status:
  Mode: Master/Client
  Radio: ON/OFF
  Client ID: <0-15>
  Connection: Connected/Disconnected
  Telnet Sessions: <count>
  Commands: <count>
  Uptime: <seconds> sec
```

#### `who`
Show master/client connection information.

**Master mode:**
```
Master/Client Information:
  Mode: MASTER
  Client[0]: CALLSIGN1
  Client[1]: CALLSIGN2
```

**Client mode:**
```
Master/Client Information:
  Mode: CLIENT
  Client ID: 5
  Connection: Connected
  Master: MASTER_CALL
```

### Display Commands

#### `show config` or `display config`
Display current configuration.
```
Configuration:
  Network ID: 0
  Frequency: 437.000 MHz
  Mode: Master/Client
  Modulation: 22
  Radio State: ON/OFF
```

#### `show tasks`
Display FreeRTOS task information.
```
FreeRTOS Tasks (10):
  RadioISR        Pri:5 Stack:128
  RadioProc       Pri:4 Stack:256
  TDMA            Pri:3 Stack:384
  ...
```

#### `show memory`
Display memory usage statistics.
```
Memory Usage:
  Heap Free: 8192 bytes
  Heap Min:  7456 bytes
  RAM Used:  64648 / 65536 (98.9%)
```

#### `show dhcp` or `display DHCP_ARP`
Display DHCP/ARP table entries.
```
DHCP/ARP Entries:
  [0] 192.168.1.10  STATION1
  [1] 192.168.1.11  STATION2
  [2] 192.168.1.12  STATION3
```

### Control Commands

#### `radio on`
Enable radio transmitter.

#### `radio off`
Disable radio transmitter.

### Configuration Commands

#### `set network_id <0-15>` or `set radio_netw_ID <0-15>`
Set radio network ID (0-15).
```
set network_id 5
Network ID set to 5
```

#### `set frequency <MHz>`
Set operating frequency (420-450 MHz).
```
set frequency 437.000
Frequency set to 437.000 MHz
```

#### `set modulation <11-14|20-24>`
Set modulation scheme.

Valid values:
- 11-14: Lower data rates
- 20-24: Higher data rates

```
set modulation 22
Modulation set to 22
```

#### `set is_master <yes|no>`
Set master/client operating mode.
```
set is_master yes
Master mode enabled
```

#### `set callsign <CALLSIGN>`
Set station callsign (max 13 characters).
```
set callsign OH1CALL
Callsign set to OH1CALL
```

### System Commands

#### `save`
Save configuration to non-volatile memory.
```
Configuration save not yet implemented.
```

#### `reboot`
Restart the modem.
```
Rebooting...
```

#### `reset_to_default`
Factory reset - clear all configuration and reboot.
```
Clearing config and rebooting...
```

#### `exit` or `logout`
Close telnet connection.
```
Goodbye!
```

#### `73`
Ham radio goodbye (Easter egg).
```
73!
```

## Command Parameters

### Available `set` parameters:
- `network_id` or `radio_netw_ID` - Radio network ID (0-15)
- `frequency` - Operating frequency in MHz (420.000-450.000)
- `modulation` - Modulation scheme (11-14 or 20-24)
- `is_master` - Master mode (yes/no or 1/0)
- `callsign` - Station callsign (up to 13 chars)

### Available `show`/`display` parameters:
- `config` - Current configuration
- `tasks` - FreeRTOS task list
- `memory` - Memory usage
- `dhcp` or `DHCP_ARP` - DHCP/ARP table

## Examples

### Basic Configuration
```
> set callsign STATION1
Callsign set to STATION1
ready> set network_id 5
Network ID set to 5
ready> set frequency 437.000
Frequency set to 437.000 MHz
ready> set is_master yes
Master mode enabled
ready> radio on
Radio is now ON.
ready> save
Configuration save not yet implemented.
ready> 
```

### Monitoring
```
> status
Modem Status:
  Mode: Master
  Radio: ON
  Client ID: 0
  Connection: Connected
  Telnet Sessions: 5
  Commands: 42
  Uptime: 3600 sec
ready> who
Master/Client Information:
  Mode: MASTER
  Client[0]: STATION1
  Client[1]: STATION2
ready> show dhcp
DHCP/ARP Entries:
  [0] 192.168.1.10  STATION1
  [1] 192.168.1.11  STATION2
ready>
```

### Troubleshooting
```
> show tasks
FreeRTOS Tasks (10):
  RadioISR        Pri:5 Stack:128
  RadioProc       Pri:4 Stack:256
  ...
ready> show memory
Memory Usage:
  Heap Free: 8192 bytes
  Heap Min:  7456 bytes
  RAM Used:  64648 / 65536 (98.9%)
ready> show config
Configuration:
  Network ID: 5
  Frequency: 437.000 MHz
  Mode: Master
  Modulation: 22
  Radio State: ON
ready>
```

## Implementation Details

- **Total Commands**: 18 fully functional commands
- **Flash Usage**: 52,396 bytes (19.9% of 256KB)
- **RAM Usage**: 64,648 bytes (98.9% of 64KB)
- **Code**: ~550 lines in task_telnet.c
- **Features**: Parameter validation, error messages, echo support, line editing

## Notes

- Configuration changes take effect immediately
- Use `save` command to persist changes (when implemented)
- Factory reset via `reset_to_default` clears all settings
- Session timeout: 5 minutes of inactivity
- All commands are case-sensitive
- Multiple aliases supported for common commands (show/display, exit/logout)
