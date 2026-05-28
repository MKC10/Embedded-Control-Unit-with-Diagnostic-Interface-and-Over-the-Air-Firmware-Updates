# Test Log

## Bootloader UART Test

Result: Passed

Bootloader printed repeatedly through PuTTY:

```text
BOOTLOADER UART TEST
```

## Bootloader to Firmware Jump Test

Result: Passed

Observed output:

```text
BOOTLOADER STARTED
APP SP: 0x20020000
APP RESET: 0x08010951
JUMPING TO APPLICATION

APP STARTED
FIRMWARE UART TEST
```

## UART Diagnostic RX Test

Result: Partial

UART RX byte reception verified.  
Command response handling is still under debug.
