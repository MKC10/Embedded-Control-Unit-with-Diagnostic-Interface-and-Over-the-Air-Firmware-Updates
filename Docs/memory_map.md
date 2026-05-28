# Flash Memory Map

| Region | Address | Purpose |
|---|---:|---|
| Bootloader | `0x08000000` | Startup, validation, firmware handoff |
| Metadata Page | `0x08008000` | Future update flags, CRC, active bank, rollback state |
| Firmware Bank A | `0x08010000` | Active ECU firmware application |
| Firmware Bank B | `0x08050000` | Future firmware update staging area |

## Current Status

The bootloader currently validates and jumps to the firmware image at Bank A.

Verified output:

```text
BOOTLOADER STARTED
APP SP: 0x20020000
APP RESET: 0x08010951
JUMPING TO APPLICATION
APP STARTED
FIRMWARE UART TEST
```
