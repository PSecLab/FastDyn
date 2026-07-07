# Idle-Starvation Diagnostic State

This file tracks GDB/LLM diagnostic phases for idle-starvation failures. It is intentionally simple and append-only.

Before editing `gdbscripts/ardurover_script.gdb`, read this file and the current GDB script. Continue from the latest confirmed stage instead of restarting from early boot or unrelated subsystems.

## Current Confirmed Stage

- Timestamp: 2026-07-02 14:56 EDT
- Firmware/config: `configs/rover462.toml`, ArduRover v4.6.2 CubeBlack
- Latest run exit: `manual_stop` for probe-run; focused GDB diagnostic stopped after logging no-new-coverage PC inside `spi_lld_exchange`.
- Current blocker: the latest generated `dma2.c`/`spi4.c` models moved the previous SPID4 blocker forward, but boot still does not reach `_init_gyro`, `AP_InertialSensor::wait_for_sample`, `AP_Scheduler::run`, or `AP_Scheduler::loop`. The defended blocker is now the SPID1 asynchronous SPI DMA exchange in the SPI1 barometer DeviceBus callback.
- Evidence: `AP_BoardConfig::init ret=<none>`; `Rover::startup_INS enter`; `Invensense::start` reaches `after_periodic_callback` at `0x0811a878` and `before_withsem_destructor` at `0x0811a87a`. The first concrete no-new point is `AP_Baro_MS56XX::_timer` -> `_read_adc` -> `ChibiOS::SPIDevice::transfer` -> `spi_lld_exchange` at `0x0814bc6e` on `SPID1`, `n=4`; CubeBlack maps SPI1 RX/TX to DMA2 streams 2/5. Summary counters remain `scheduler_loop=0`, `scheduler_run=0`, `wait_sample_enter=0`, `gyro_init=0`.
- Next diagnostic focus: DMA2/SPI1 transfer-completion and IRQ wakeup semantics for the SPID1 barometer exchange. Ensure DMA2 stream 2 RX completion raises IRQn 58/exception 74 when TCIE is set and resumes the suspended SPI thread.
- Do not re-add unless regression evidence: IOMCU/CRC probes, early boot probes, FRAM/RAMTRON probes, board-validation SPI CS routing, or IMU WHOAMI/slave creation.

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

### 2026-06-30 14:02 EDT - INS Backend Start Timing Starvation

- Run symptom: probe exits with `no_new_coverage`; latest `probe_result.json` reports `pc=0x0813b820`, current thread `UART6`, and recent RTOS switches among high-priority service threads.
- GDB evidence: board validation passes and `Rover::startup_INS` enters. `AP_InertialSensor::_start_backends` starts backend 0. `AP_InertialSensor_Invensense::start` registers gyro/accel, reaches the checkpoint after `register_periodic_callback`, and MPU9250 FIFO polling produces samples plus raw accel/gyro callbacks. The script never observes `Invensense::start` returning, `_start_backends` returning, or `_init_gyro` entering.
- Identified blocker: likely ChibiOS timing/scheduler starvation, not a missing SPI slave. The service threads appear to wake repeatedly after high-rate periodic callback registration, preventing the lower-priority main initialization thread from making forward progress.
- Routing written: `fastdyn_work/routing.json` with `handled=false`.
- Models requested: update `tim5.c` and `systick.c`; context `nvic.c`, `scb.c`, `spi1.c`, `spi4.c`, `dma2.c`, `uart6.c`, and `tim2.c`.
- Result after implementation: pending.
- Expected next stage: `Invensense::start` should return, `_start_backends` should continue through the remaining backends, and boot should reach `_init_gyro`/INS calibration rather than spinning in high-priority service threads.
- Do not re-add unless regression evidence: board-validation probes, SPI CS/GPIO signal routing, or IMU WHOAMI/slave model creation.

### 2026-07-02 14:35 EDT - SPID4 DMA Completion Routing

- Run symptom: latest probe-run used the rebuilt TIM5 model and was stopped manually after continued service-thread churn; focused GDB stopped at the configured no-new-coverage PC inside `spi_lld_exchange`.
- GDB evidence: board validation still passes and `Rover::startup_INS` enters. `AP_InertialSensor_Invensense::start` registers gyro/accel and reaches `after_periodic_callback` at `0x0811a878` plus `before_withsem_destructor` at `0x0811a87a`. It still never reaches `_init_gyro`, `AP_InertialSensor::wait_for_sample`, `AP_Scheduler::run`, or `AP_Scheduler::loop`. The stopped stack is `AP_Baro_MS56XX::_timer` -> `_read_adc` -> `ChibiOS::SPIDevice::transfer` -> `spi_lld_exchange` at `0x0814bc6e` on `SPID4`, `n=4`.
- Identified blocker: SPID4 asynchronous DMA exchange completion is not yet a defended working contract. CubeBlack `hwdef.h` maps SPI4 RX to DMA2 stream 3 and SPI4 TX to DMA2 stream 4; the firmware suspends the SPI thread after `spiStartExchangeI()` and expects DMA2 Stream3 RX completion IRQn 59/exception 75 to run `spi_lld_serve_rx_interrupt` and resume it.
- Routing written: `fastdyn_work/routing.json` with `handled=false`.
- Models requested: update `dma2.c` and `spi4.c`; context `ms5611_spi4.c`, `nvic.c`, `tim5.c`, and `spi1.c`.
- Result after implementation: pending.
- Expected next stage: SPID4 barometer callback should complete and return to the DeviceBus thread without trapping progress in `spi_lld_exchange`; boot should then resume INS backend startup and reach `_init_gyro` or a later, more precise blocker.
- Do not re-add unless regression evidence: board-validation probes, SPI CS/GPIO signal routing, IMU WHOAMI/slave model creation, or IOMCU/CRC probes.

### 2026-07-02 14:56 EDT - SPID1 DMA Completion Routing

- Run symptom: latest `probe_result.json` exits via `manual_stop`, current RTOS thread is `SPI1`, and recent switches are dominated by `monitor`, `SPI1`, `IOMCU`, `UART6`, and timer/rcout activity. The latest generated `dma2.c`, `spi4.c`, `dma2.so`, and `spi4.so` were rebuilt at 14:52 and the run artifacts are from 14:56.
- GDB evidence: the previous SPID4 no-new site moved forward. Fresh GDB now stops at `spi_lld_exchange` with `spip=0x20018c60 <SPID1>`, `n=4`, from `AP_Baro_MS56XX::_timer` -> `_read_adc` -> `ChibiOS::SPIDevice::transfer`. Summary still shows `scheduler_loop=0`, `scheduler_run=0`, `wait_sample_enter=0`, and `gyro_init=0`.
- Identified blocker: SPID1 asynchronous DMA exchange completion is not yet a defended working contract. CubeBlack `hwdef.h` maps SPI1 RX to DMA2 stream 2 and SPI1 TX to DMA2 stream 5; the firmware suspends the SPI thread after `spiStartExchangeI()` and expects DMA2 Stream2 RX completion IRQn 58/exception 74 to run `spi_lld_serve_rx_interrupt` and resume it.
- Routing written: `fastdyn_work/routing.json` with `handled=false`.
- Models requested: update `dma2.c` and `spi1.c`; context `spi4.c`, `ms5611_spi1.c`, `mpu9250_spi.c`, `nvic.c`, and `tim5.c`.
- Result after implementation: pending.
- Expected next stage: SPID1 barometer callback should complete and return to the DeviceBus thread without trapping progress in `spi_lld_exchange`; boot should then resume INS backend startup and reach `_init_gyro` or a later, more precise blocker.
- Do not re-add unless regression evidence: board-validation probes, SPI CS/GPIO signal routing, IMU WHOAMI/slave model creation, or IOMCU/CRC probes.
