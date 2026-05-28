# Firmware Architecture

The firmware application runs from `0x08010000` after bootloader handoff.

## Modules

| Module | Purpose |
|---|---|
| `firmware.c / firmware.h` | Main ECU application flow |
| `diagnostic.c / diagnostic.h` | UART command interface |
| `state_machine.c / state_machine.h` | NORMAL, WARNING, FAULT, RECOVERY logic |
| `sht31.c / sht31.h` | SHT31 I2C temperature/humidity driver |
| `bmp280.c / bmp280.h` | BMP280 SPI pressure/temperature driver |
| `main.c` | HAL startup, peripheral init, VTOR setup, firmware entry |

## Runtime Flow

1. Firmware starts after bootloader jump.
2. VTOR is set to `0x08010000`.
3. Interrupts are re-enabled.
4. Peripherals are initialized.
5. Firmware enters the ECU application loop.
6. TIM2 provides periodic update ticks.
7. Diagnostic UART processes commands from PuTTY.

## Notes

Real sensor acquisition belongs in the firmware application, not in the bootloader. The bootloader only validates and launches the firmware.
