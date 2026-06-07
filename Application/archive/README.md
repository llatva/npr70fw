# Archived Task Files

This directory contains task implementation files that have been replaced by combined/refactored versions during the FreeRTOS port.

## Archived Files (June 07, 2026)

### Radio Tasks (replaced by task_radio_combined.c/h)
- `task_radio_isr.c/h` - Original separate ISR handler task
- `task_radio_processing.c/h` - Original separate radio packet processing task

**Replacement**: These were combined into `task_radio_combined.c/h` for better efficiency and reduced context switching overhead.

### Ethernet Tasks (replaced by task_ethernet.c/h)
- `task_ethernet_rx.c/h` - Original separate Ethernet RX task
- `task_ethernet_tx.c/h` - Original separate Ethernet TX task

**Replacement**: These were combined into `task_ethernet.c/h` for unified packet routing and ARP proxy implementation.

### Network Management Tasks
- `task_networkmgmt.c/h` - Duplicate/incomplete version (replaced by task_network_mgmt.c)
- `task_netmgmt_telnet.c/h` - Not wired into the system, Telnet functionality is in task_telnet.c

**Note**: These files are kept for reference purposes only. They may contain useful code snippets or logic that could be referenced during future development, but they are NOT compiled or used in the current build.

## Safe to Delete?

Yes, these files can be safely deleted if you're confident the new combined implementations are complete and tested. The original mbed OS source code is preserved in the `source/` directory for reference.

## Version History

- **v1.6 (2025-01-14)**: Files archived during TODO-14 cleanup
