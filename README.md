# ECU Dual-Bank OTA Bootloader — STM32F446RE

> Bare-metal embedded control unit with field-upgradeable firmware — no RTOS, no heap, deterministic 10 Hz control loop.

![Platform](https://img.shields.io/badge/MCU-STM32F446RE-blue?style=flat-square)
![Language](https://img.shields.io/badge/Language-C%20%28bare--metal%29-lightgrey?style=flat-square)
![IDE](https://img.shields.io/badge/IDE-STM32CubeIDE-03234B?style=flat-square)
![OTA](https://img.shields.io/badge/OTA-UART%20%2B%20AWS%20S3-orange?style=flat-square)
![Status](https://img.shields.io/badge/Status-Hardware%20Verified-brightgreen?style=flat-square)

---

## What This Is

A production-style bootloader and firmware stack for the STM32F446RE Nucleo board.  
The bootloader manages two independent flash banks — Bank A (golden/fallback) never changes; Bank B receives field updates over UART. A Python host agent or an **ESP32-S3 WiFi bridge** fetches firmware from AWS S3, streams it to the STM32, and the bootloader handles validation, trial-boot, confirm, and automatic rollback — all in bare-metal C with zero dynamic allocation.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        STM32F446RE Flash (512KB)                        │
│                                                                         │
│  S0–S2 │  S3 (Metadata)  │  S4–S5 (Bank A)  │  S6–S7 (Bank B)        │
│  48KB  │     16KB         │     192KB         │     256KB              │
│ Bootldr│  fw_meta_t       │  Golden Image     │  Field-Update Image    │
│        │  State/CRC/Ver   │  Never Erased     │  PENDING→TRIAL→CONF   │
└─────────────────────────────────────────────────────────────────────────┘

Boot Decision Flow:
  RESET
    └─► Read Metadata (S3)
          ├─► Bank B PENDING/TRIAL?  ──► CRC-validate → jump Bank B (trial)
          ├─► Bank B CONFIRMED?      ──► jump Bank B (stable)
          └─► else                   ──► validate + jump Bank A (golden)
```

```
Cloud OTA Pipeline (ESP32-S3 WiFi Bridge):

  AWS S3 Bucket
  └─► latest.json  ──►  ESP32-S3 (WiFi)
                            ├─ HTTPS fetch firmware.bin
                            ├─ SHA-256 stream-verify
                            └─ UART → STM32 USART1 (PA9/PA10)
                                          │
                                    OTA Protocol
                                    START → DATA×N → END → STATUS
                                          │
                                    Bank B Flash
                                    PENDING → TRIAL → CONFIRMED
```

---

## Key Features

| Feature | Detail |
|---|---|
| **Dual-bank layout** | Bank A golden (0x08010000, 192KB), Bank B updatable (0x08040000, 256KB) |
| **OTA protocol** | Custom UART framing — START / DATA (256B chunks) / END / STATUS |
| **Integrity** | CRC-32 (IEEE 802.3) per-image + CRC-8 Sensirion per sensor frame |
| **Trial-boot + rollback** | Bad image → automatic fallback to Bank A, 3-attempt limit |
| **Release counter** | Monotonic `bankB_version` field in metadata, human-readable freshness label |
| **Cloud delivery** | AWS S3 public-read bucket, `latest.json` update pointer, `metadata.json` per release |
| **WiFi bridge** | ESP32-S3 fetches from S3 over HTTPS, SHA-256 verifies, forwards via UART |
| **Sensors** | SHT31 (I2C), BMP280 (SPI) — independent fault isolation, no shared Error_Handler |
| **Zero dynamic allocation** | No malloc/free anywhere in the stack |
| **Deterministic loop** | 10 Hz control loop, bare-metal HAL only (no RTOS) |

---

## Flash Map

```
Address         Size    Contents
0x08000000      48KB    Bootloader (S0–S2)
0x0800C000      16KB    Metadata sector S3 — fw_meta_t struct
0x08010000     192KB    Bank A — Golden/Fallback firmware (S4–S5)
0x08040000     256KB    Bank B — Field-update firmware (S6–S7)
```

---

## OTA Protocol

```
Host → STM32:

  START  [0x01][size u32 LE][reserved u16 LE]
  DATA   [0x02][len u16 LE][payload up to 256B]  × N
  END    [0x03][crc32 u32 LE — whole image]
  STATUS [0x04]  ← query only; STM32 replies with 32-byte struct

Per-command ACK/NAK:  0x06 = ACK,  0x15 = NAK
```

Bank B state machine:
```
BANK_EMPTY → PENDING (after END+CRC OK) → TRIAL (first boot) → CONFIRMED (app calls ConfirmBoot)
                                                              └→ FAILED (3 trial attempts exhausted)
```

---

## Repository Structure

```
ECU-Bootloader/
├── Bootloader/
│   ├── Core/Src/
│   │   ├── bootloader.c        # BL_JumpToApp() — Thumb-bit-safe jump
│   │   ├── boot_decision.c     # Bank selection logic + trial counting
│   │   ├── fw_metadata.c       # Metadata read/write (S3 sector)
│   │   ├── uart_fw_receiver.c  # OTA protocol state machine
│   │   ├── fw_flash.c          # HAL_FLASH_Program wrapper
│   │   └── diagnostic.c        # HELP/STATUS/SENSORS/FAULT/RESET commands
│   └── LinkerScript.ld         # Parameterized for Bank A/Bank B builds
│
├── Firmware/
│   ├── Core/Src/
│   │   ├── firmware.c          # 10Hz control loop + sensor reads
│   │   ├── sht31.c             # I2C driver (CRC-8 Sensirion)
│   │   └── bmp280.c            # SPI driver
│   └── LinkerScript_BankA.ld
│   └── LinkerScript_BankB.ld
│
├── ESP32_WiFi_Bridge/
│   └── esp32_ota_bridge.ino    # Arduino C++ — S3 fetch + SHA-256 + UART forward
│
├── host/
│   └── fw_host.py              # Python OTA host agent (USB-UART fallback)
│
└── docs/
    ├── architecture.md
    ├── hardware-setup.md
    └── bug-log.md              # 21-entry production bug log
```

---

## Notable Bugs Fixed (Production Debug Log)

This project accumulated a 21-entry bug log across development. Highlights:

| # | Bug | Root Cause | Fix |
|---|-----|-----------|-----|
| 1 | App silent after bootloader jump — all week | `BL_JumpToApp()` stripped Thumb bit before calling function pointer; ARM Cortex-M faults immediately on non-Thumb-mode call | Use cleaned address only for range-check; call through original Thumb-bit-intact vector |
| 2 | `fw_metadata.c` caused HardFault on cold boot | `FW_Meta_Read` never populated caller's struct on non-blank-flash paths — uninitialized stack garbage fed into CRC check | Fixed struct population for all flash-state branches |
| 3 | HAL_BUSY stuck in UART receiver | `UART_FW_Receiver_Init()` armed IT-mode RX before blocking `HAL_UART_Receive` calls | Remove IT-mode arm; use blocking mode only |
| 4 | ST-Link contended USART2 (PA2/PA3) | ST-Link's UART transceiver physically drives those pins while powered; solder bridges SB62/SB63 gate CN9 D0/D1 — asymmetrically open | Migrated OTA protocol to USART1 (PA9/PA10 on CN10, no solder-bridge gating) |
| 5 | Post-clock-config hang after bootloader jump | Bootloader's active PLL was still SYSCLK source; app's `SystemClock_Config()` can't reconfigure a PLL in use as SYSCLK → `HAL_ERROR` → `Error_Handler()` | Switch to HSI + disable PLL before calling `SystemClock_Config()` |
| 6 | ESP32 chunk short-reads caused NAK storm | Single `stream->readBytes()` per chunk — network stall returned <256 bytes, non-word-aligned write failed HAL_FLASH_Program | Loop reads per chunk until full CHUNK_SIZE collected |

Full 21-entry log: [`docs/bug-log.md`](docs/bug-log.md)

---

## Hardware Setup

**Components:**
- STM32 Nucleo-F446RE
- ESP32-S3 dev board (WiFi OTA bridge)
- SHT31 breakout (I2C — temperature/humidity)
- BMP280 breakout (SPI — pressure/temperature)
- External 4.7kΩ pull-ups on SDA/SCL

**Key Wiring (OTA path):**
```
ESP32-S3 GPIO16 (TX) ──► STM32 PA10 (USART1 RX)  [CN10 pin 33]
ESP32-S3 GPIO17 (RX) ◄── STM32 PA9  (USART1 TX)  [CN10 pin 21]
GND ──────────────────── GND
(Separate USB power per board)
```

**Sensor Wiring:**
```
SHT31:  SDA → PB9 (I2C1_SDA),  SCL → PB8 (I2C1_SCL),  4.7kΩ pull-ups to 3.3V
BMP280: MOSI→PA7, MISO→PA6, SCK→PA5, CS→PB0 (SPI1),  CSB→3.3V, SDO→GND
```

---

## Build & Flash

**Requirements:** STM32CubeIDE 1.x, STM32CubeProgrammer (or ST-Link)

```bash
# Build bootloader
# Open Bootloader/ in STM32CubeIDE → Build → Debug configuration

# Build firmware (Bank A or Bank B)
# Select LinkerScript_BankA.ld or LinkerScript_BankB.ld in project properties → C/C++ Build → Settings → Linker Script

# Flash via STM32CubeProgrammer
# Erase full chip first → Program bootloader.bin at 0x08000000
# Program firmware.bin at 0x08010000 (Bank A)
```

**Trigger OTA update:**
```
1. Hold B1 (PC13) during reset to enter OTA mode
2. Bootloader prints: "OTA RX READY — WAITING FOR HOST"
3. Run Python host agent OR let ESP32 bridge auto-deliver from S3
4. After successful update:  PENDING → next reset → TRIAL → (app calls ConfirmBoot) → CONFIRMED
```

**Python host agent (USB fallback):**
```bash
pip install pyserial
python host/fw_host.py --port COM3 --firmware firmware_v2.bin
```

---

## AWS S3 Layout

```
s3://firmwareupdatedualbankbootloader-<account>-us-east-2-an/
└── Firmware_STM32_Update/
    ├── latest.json              # { "version": "v2.0.0", "url": "...", "sha256": "..." }
    ├── v1.0.0/
    │   ├── firmware.bin
    │   └── metadata.json
    └── v2.0.0/
        ├── firmware.bin
        └── metadata.json        # { version, size, sha256, target_bank, rollback_flags }
```

---

## What's Not Done Yet

| Item | Status |
|---|---|
| `FW_Update_ConfirmBoot()` called from application | Not implemented — trial→confirmed works but app doesn't call confirm yet |
| IWDG watchdog (rollback on hung valid image) | Designed, not implemented |
| CAN diagnostic interface (STATUS/SENSORS/FAULT over bxCAN) | Module designed, loopback validation pending hardware (transceiver on order) |
| Method 2 crypto signing (ECDSA + mbedtls on ESP32) | Next hardening milestone |
| `boot_decision.c` debug instrumentation strip | Still has `META:/BANK B:/BOOT_A:` debug prints |

---

## Skills Demonstrated

`Bare-metal C` · `STM32 HAL` · `Flash memory management` · `Custom binary protocol` · `CRC-32/CRC-8` · `Interrupt-free UART` · `Boot decision logic` · `Trial/confirm/rollback state machine` · `ESP32 Arduino C++` · `AWS S3 integration` · `SHA-256 stream verification` · `I2C / SPI sensor drivers` · `Multi-bank linker scripts` · `Hardware debugging`

---

## Timeline

| Milestone | Date |
|---|---|
| Dual-bank bootloader running on hardware | Jun 2026 |
| End-to-end OTA cycle (UART + Python) working | Jul 15 2026 |
| Thumb-bit bug found + both banks verified | Jul 21 2026 |
| ESP32-S3 WiFi bridge end-to-end verified | Jul 29 2026 |
| Release counter + freshness labels working | Jul 29–30 2026 |
| SHT31 + BMP280 sensor reads confirmed on hardware | Aug 5 2026 |

---

*Built as a graduate portfolio project (MS ECE, UAB). All functionality hardware-verified on real Nucleo-F446RE board.*
