# Diagnostic Commands

UART: USART2 through ST-Link VCP  
Baud rate: 115200  
Pins: PA2 TX, PA3 RX  

| Command | Purpose |
|---|---|
| `HELP` | List available commands |
| `STATUS` | Print current ECU state and fault code |
| `SENSORS` | Print SHT31 and BMP280 readings |
| `FAULT` | Print active fault information |
| `RESET` | Trigger software reset |

## Current Debug Status

UART TX works. UART RX byte reception has been verified.

Current remaining work: finalize command buffering and response execution for commands such as `HELP`.

## PuTTY Settings

```text
Connection type: Serial
Speed: 115200
Data bits: 8
Stop bits: 1
Parity: None
Flow control: None
Local echo: Force off
Local line editing: Force off
```
