# Idle-Starvation Diagnostic State

This file tracks GDB/LLM diagnostic phases for idle-starvation failures. It is intentionally simple and append-only.

Before editing `gdbscripts/ardurover_script.gdb`, read this file and the current GDB script. Continue from the latest confirmed stage instead of restarting from early boot or unrelated subsystems.

## Current Confirmed Stage

- Timestamp: 2026-06-29 17:50 EDT
- Firmware/config: `configs/rover462.toml`, ArduRover v4.6.2 CubeBlack
- Latest run exit: board validation fatal after GDB diagnostic run logged in `fastdyn_work/gdb_diag.log`
- Current blocker: `CHECK_IMU2_PRESENT` fails because `mpu9250` on SPI1 CS4/GPIOC2/signal_id=34 is not modeled/routed; `spi_check_register("mpu9250", reg=0x75, expected=0x71)` returns false.
- Evidence: `AP_RAMTRON::init ret=true`; `AP_Param::load_all ret=true`; `check_ms5611 dev=ms5611 ret=true`; `check_ms5611 dev=ms5611_ext ret=true`; `spi_check_register dev=icm20602_ext reg=0x75 expected=0x12 ret=true`; `spi_check_register dev=icm20948_ext reg=0x00 expected=0xea ret=true`; `spi_check_register dev=mpu9250 reg=0x75 expected=0x71 ret=false`.
- Next diagnostic focus: SPI1 CS4 routing through GPIOC2/signal_id=34 and a new `mpu9250` SPI slave on SPI1 CS4.
- Do not re-add unless regression evidence: IOMCU/CRC probes, early boot probes, FRAM/RAMTRON probes, SPI4 CS1/CS4 fixes.

## Phase Log

### Template - YYYY-MM-DD HH:MM TZ - Short Stage Name

- Run symptom:
- GDB evidence:
- Identified blocker:
- Routing written:
- Models requested:
- Result after implementation:
- Expected next stage:
- Do not re-add unless regression evidence:

### 2026-06-29 16:38 EDT - SPI4 IMU0 Alternative B Routing

- Run symptom: `Board Validation %s Failed` after both barometer probes completed.
- GDB evidence: `ms5611` on SPI1 CS3/GPIOD7/signal_id=55 returned true; `ms5611_ext` on SPI4 CS2/GPIOC14/signal_id=46 returned true; `mpu9250_ext` on SPI4 CS1/GPIOE4/signal_id=68 read WHOAMI `0xff` instead of `0x71`; `icm20602_ext` on SPI4 CS4/GPIOC13/signal_id=45 read WHOAMI `0xff` instead of `0x12`.
- Identified blocker: `CHECK_IMU0_PRESENT`; current models lack a working Alternative B SPI4 IMU path. `spi4.c` still uses heuristic CS selection and `gpioc.c` is a stub, while `icm20602_spi.c` is missing.
- Routing written: `fastdyn_work/routing.json` with `handled=false`.
- Models requested: context `spi2.c`, `ms5611_spi4.c`, `gpiod.c`; update `gpioc.c`, `spi4.c`; create `icm20602_spi.c` on `spi4` connection `CS: device=4`.
- Result after implementation: pending.
- Expected next stage: `CHECK_IMU0_PRESENT` should pass via `icm20602_ext`; next likely failure is `CHECK_IMU1_PRESENT` or `CHECK_IMU2_PRESENT`.
- Do not re-add unless regression evidence: IOMCU/CRC probes, early boot probes, SPI1-only BARO0 probes.

### 2026-06-29 17:24 EDT - SPI4 IMU1 ICM20948 Routing

- Run symptom: `Board Validation %s Failed` after the corrected TOML added `cs_id=4` for `icm20602_spi.c`.
- GDB evidence: `ms5611` and `ms5611_ext` both returned true; `icm20602_ext` on SPI4 CS4/GPIOC13/signal_id=45 returned WHOAMI `0x12` and passed; `lsm9ds0_ext_g` on CS4 returned `0x00` and failed; `icm20948_ext` on SPI4 CS1/GPIOE4/signal_id=68 returned `0x00` instead of `0xea`.
- Identified blocker: `CHECK_IMU1_PRESENT`; current models lack a working newer-path `icm20948_ext` slave on SPI4 CS1 and SPI4/GPIOE CS routing is incomplete.
- Routing written: `fastdyn_work/routing.json` with `handled=false`.
- Models requested: context `spi2.c`, `gpiod.c`, `gpioc.c`, `icm20602_spi.c`; update `gpioe.c`, `spi4.c`; create `icm20948_spi.c` on `spi4` connection `CS: device=1`.
- Result after implementation: pending.
- Expected next stage: `CHECK_IMU1_PRESENT` should pass via `icm20948_ext`; next likely failure is `CHECK_IMU2_PRESENT` for internal `mpu9250`.
- Do not re-add unless regression evidence: IOMCU/CRC probes, early boot probes, SPI1-only BARO0 probes, SPI4 CS4/ICM20602 routing fixes.

### 2026-06-29 17:50 EDT - SPI1 IMU2 MPU9250 Routing

- Run symptom: GDB stopped at `AP_BoardConfig::throw_error` with `Board Validation %s Failed`.
- GDB evidence: FRAM and parameter storage are no longer the blocker (`AP_RAMTRON::init ret=true`, `AP_Param::load_all ret=true`). `ms5611`, `ms5611_ext`, `icm20602_ext`, and `icm20948_ext` all pass. `mpu9250_ext` and `lsm9ds0_ext_g` fail, but they are optional alternatives in `HAL_VALIDATE_BOARD`; the final required `mpu9250` check fails with `spi_check_register dev=mpu9250 reg=0x75 expected=0x71 ret=false`.
- Identified blocker: `CHECK_IMU2_PRESENT`; current TOML has no SPI1 CS4 `mpu9250` slave, and `spi1.c` only routes GPIOD7/signal_id=55 to the first SPI1 slave.
- Routing written: `fastdyn_work/routing.json` with `handled=false`.
- Models requested: context `spi2.c`, `gpiod.c`, `ms5611_spi1.c`, `icm20602_spi.c`; update `gpioc.c`, `spi1.c`; create `mpu9250_spi.c` on `spi1` connection `CS: device=4`.
- Result after implementation: pending.
- Expected next stage: `CHECK_IMU2_PRESENT` should pass via internal `mpu9250`; next likely failure is sensor driver initialization or compass probing through the MPU9250 path.
- Do not re-add unless regression evidence: IOMCU/CRC probes, early boot probes, FRAM/RAMTRON probes, SPI4 CS1/CS4 fixes.
