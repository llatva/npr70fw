# New Packet Radio

   This is the source code for the NPR-70 modem firmware by F4HDK.

   This code is based on the 2020_02_23 release available at https://hackaday.io/project/164092-npr-new-packet-radio .

   It adds a couple of features such as SNMP support (see the included MIB), as well as a refactor of settings (which makes it possible to update settings across firmware releases if needed) and reduced memory usage.

## Documentation

### Memory Analysis
Comprehensive memory consumption analysis for this RAM-constrained system:

- **[Memory Quick Reference](docs/memory-quick-reference.md)** - Quick reference guide with key statistics
- **[Memory Analysis](docs/memory-analysis.md)** - Detailed analysis of all buffer allocations and memory usage
- **[Buffer Comparison](docs/buffer-comparison.md)** - Side-by-side comparison of configurations with and without external SRAM

These documents analyze RAM usage on the STM32L432KC (64KB RAM) for both "no external SRAM" and "external SRAM installed" configurations.

Ed
