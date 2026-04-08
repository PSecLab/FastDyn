# I2C Polling – BME280 Write/Read Test (STM32H743ZI)

## Overview

This firmware validates two fundamental I2C operations against a BME280 sensor over a polling-based I2C driver:

| # | Test Case | Action | Pass Condition |
|---|-----------|--------|----------------|
| TC1 | **Write a byte to slave** | Write soft-reset command `0xB6` to register `0xE0` | `HAL_I2C_Mem_Write` returns `HAL_OK` |
| TC2 | **Read a byte from slave** | Read chip ID from register `0xD0` | Received value equals `0x60` |

---

## Hardware

### Board
- **NUCLEO-H743ZI** (STM32H743ZI)

### Sensor
- **Bosch BME280** (same module used in the SPI example)

### I2C Peripheral
- **I2C1** — APB1 bus, 400 kHz

### Pin Connections

| NUCLEO-H743ZI Pin | Connector | Signal | BME280 Pin |
|-------------------|-----------|--------|------------|
| PB8               | CN7-2 (D15) | SCL  | SCK / SCL  |
| PB9               | CN7-4 (D14) | SDA  | SDI / SDA  |
| 3V3               | CN8-7       | VCC  | VIN / VCC  |
| GND               | CN8-11      | GND  | GND        |
| GND               | —           | Addr | SDO (sets address = 0x76) |
| 3V3               | —           | I2C mode | CSB (pull high) |

> **Pull-up resistors**: 4.7 kΩ on both SCL and SDA to 3V3.
> Most BME280 breakout boards (e.g., Adafruit) include these on-board.

### I2C Address
- `SDO = GND` → address **0x76** (default, as configured in firmware)
- `SDO = 3V3` → address **0x77** → change `BME280_I2C_ADDR` in `main.c` accordingly

---

## LED Indicators

| LED | Color | Meaning |
|-----|-------|---------|
| LED1 (LD1) | Green  | TC1 PASS – write byte succeeded |
| LED2 (LD2) | Yellow | TC2 PASS – read byte returned `0x60` |
| LED3 (LD3) | Red    | ERROR – any failure (fast blink) |
| LED1 + LED2 solid | — | **All tests PASSED** |

---

## How to Run

1. Build and flash the firmware using STM32CubeIDE.
2. Power on the board – **LED1 solid ON** indicating ready/waiting state.
3. Press the **blue USER button** (B1) — a short press is enough.
4. The firmware executes TC1 then TC2 automatically.

---

## Expected Passing Sequence

```
[READY]   LED1 solid ON (blocking wait)
[BUTTON]  User presses USER button (short press is fine)
[TC1]     Write 0xB6 → register 0xE0  →  LED1 solid ON  (PASS)
[WAIT]    10 ms delay for BME280 startup after reset
[TC2]     Read register 0xD0          →  LED2 solid ON  (PASS)
[DONE]    LED1 + LED2 both solid = All tests PASSED
```

## Failing Condition — Error Blink Codes

| LED pattern | Meaning | Likely cause |
|-------------|---------|--------------|
| **LED3 slow blink** (1 s) | No device found at 0x76 or 0x77 | Wrong wiring, missing CSB pull-up, bad connections |
| **LED1 + LED3 fast blink** (150 ms) | TC1 FAILED — write returned error | Device visible on bus but write rejected |
| **LED2 + LED3 fast blink** (150 ms) | TC2 FAILED — read wrong chip ID | Write OK but got unexpected value from 0xD0 |

---

## Key Firmware Details

| Parameter | Value |
|-----------|-------|
| I2C Instance | I2C1 |
| SCL Pin | PB8 |
| SDA Pin | PB9 |
| Speed | 400 kHz |
| Timing Register | `0x00901954` |
| BME280 address | `0x76` (7-bit) |
| Write register | `0xE0` (reset), data `0xB6` |
| Read register | `0xD0` (chip_id), expect `0x60` |
| Timeout | 1000 ms |
| System Clock | 400 MHz (HSE 8 MHz, PLL) |
