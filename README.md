# SlidR Firmware (v2)

This workspace contains a clean-slate ESP-IDF implementation of the SlidR firmware.

Implementation goals for v2:
- JSON-only protocol over USB UART (human-readable traffic)
- Modular parts/services architecture
- Runtime configuration updates by component id
- Explicit mutability policy per option: runtime-mutable vs restart-required

This first commit scaffolds the runtime kernel, JSON message gateway, and configuration engine contracts.
