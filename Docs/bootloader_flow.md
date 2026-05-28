# Bootloader Flow

1. MCU resets and starts execution from `0x08000000`.
2. Bootloader initializes clock, GPIO, and USART2.
3. Bootloader reads the application vector table from `0x08010000`.
4. It validates:
   - Initial stack pointer is inside SRAM.
   - Reset handler points to valid flash memory.
5. Bootloader disables interrupts and clears pending NVIC state.
6. Bootloader relocates VTOR to the firmware vector table.
7. Bootloader sets MSP to the firmware stack pointer.
8. Bootloader jumps to the firmware reset handler.

## Current Verified Behavior

The bootloader successfully jumps from `0x08000000` to the firmware at `0x08010000`.

## Future Behavior

The bootloader architecture is intended to support:

- CRC validation
- Metadata-based active bank selection
- Bank B staging
- Trial boot
- Rollback to known-good firmware
