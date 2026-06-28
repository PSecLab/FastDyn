



```bash

╭─ /scratch/Fastdyn/ardurover_r/clean_rehosting/FastDyn │ on ardurover_rehosting_main !13 ?5
╰─ gdb-multiarch -q -x gdbscripts/ardurover_script.gdb
0x08005300 in Reset_Handler ()

SPID1 = 0x20018c60
SPID4 = 0x20018bf8
Breakpoint 1 at 0x8023460: file ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp, line 461.
Breakpoint 2 at 0x8022ef8: file ../../libraries/AP_BoardConfig/board_drivers.cpp, line 182.
Breakpoint 3 at 0x8022d04: file ../../libraries/AP_BoardConfig/board_drivers.cpp, line 125.
Breakpoint 4 at 0x8022d8c: file ../../libraries/AP_BoardConfig/board_drivers.cpp, line 155.
Breakpoint 5 at 0x8139a3c: file ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp, line 450.
Breakpoint 6 at 0x813949c: file ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp, line 175.
Breakpoint 7 at 0x811a94c: file ../../libraries/AP_InertialSensor/AP_InertialSensor_Invensense.cpp, line 1002.
Breakpoint 8 at 0x811bc58: file ../../libraries/AP_InertialSensor/AP_InertialSensor_Invensensev2.cpp, line 684.
Breakpoint 9 at 0x811a8a0: file ../../libraries/AP_InertialSensor/AP_InertialSensor_Invensense.cpp, line 965.
Breakpoint 10 at 0x811bc20: file ../../libraries/AP_InertialSensor/AP_InertialSensor_Invensensev2.cpp, line 666.
Breakpoint 11 at 0x802e914: file ../../libraries/AP_Compass/AP_Compass_LSM303D.cpp, line 288.
Breakpoint 12 at 0x814b9bc: file ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c, line 315.
Breakpoint 13 at 0x814bd34: file ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c, line 658.

[spi] get_device name=ramtron

[probe] check_ms5611 dev=ms5611
#0  AP_BoardConfig::check_ms5611 (this=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d84 "ms5611") at ../../libraries/AP_BoardConfig/board_drivers.cpp:182
#1  0x08023052 in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#2  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#3  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#4  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#5  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi] get_device name=ms5611

[spi] get_device name=ms5611

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=1
0x816c808 <_ZZN14AP_BoardConfig12check_ms5611EPKcE16CMD_MS56XX_RESET>:  0x1e    0x000x00    0x00    0x54    0x72    0x15    0x08

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3
0x20002108:     0xa0    0x00    0x00    0x20    0x1b    0x9e    0x13    0x08

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3
0x20002108:     0xa2    0x00    0x00    0x00    0x1b    0x9e    0x13    0x08

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3
0x20002108:     0xa4    0x00    0x00    0x00    0x1b    0x9e    0x13    0x08

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3
0x20002108:     0xa6    0x00    0x00    0x00    0x1b    0x9e    0x13    0x08

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3
0x20002108:     0xa8    0x00    0x00    0x00    0x55    0x55    0x55    0x55

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3
0x20002108:     0xaa    0x00    0x00    0x00    0x55    0x55    0x55    0x55

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3
0x20002108:     0xac    0x00    0x00    0x00    0x55    0x55    0x55    0x55

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3
0x20002108:     0xae    0x00    0x00    0x00    0x55    0x55    0x55    0x55

[probe] check_ms5611 dev=ms5611_ext
#0  AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:182
#1  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#2  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#3  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#4  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#5  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi] get_device name=ms5611_ext

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=1 send=0x816c808 recv=(nil)
0x816c808 <_ZZN14AP_BoardConfig12check_ms5611EPKcE16CMD_MS56XX_RESET>:  0x1e    0x000x00    0x00    0x54    0x72    0x15    0x08
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x816c808 <AP_BoardConfig::check_ms5611(char const*)::CMD_MS56XX_RESET> "\036", recv=recv@entry=0x0, len=len@entry=1) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813967c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x816c808 <AP_BoardConfig::check_ms5611(char const*)::CMD_MS56XX_RESET> "\036", send_len=<optimized out>, recv=0x0, recv_len=0) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:298
#2  0x08022f46 in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:201
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x816c808 <AP_BoardConfig::check_ms5611(char const*)::CMD_MS56XX_RESET> "\036", recv=<optimized out>, recv@entry=0x0, len=len@entry=1) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813967c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x816c808 <AP_BoardConfig::check_ms5611(char const*)::CMD_MS56XX_RESET> "\036", send_len=<optimized out>, recv=0x0, recv_len=0) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:298
#7  0x08022f46 in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:201

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xa0    0x00    0x00    0x20    0x1b    0x9e    0x13    0x08
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\240", recv=recv@entry=0x20002108 "\240", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\240", send_len=1, recv=0x20002148 "", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xa0    0x00    0x00    0x20    0x1b    0x9e    0x13    0x08
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\240", recv=recv@entry=0x20002108 "\240", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\240", send_len=1, recv=0x20002148 "", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x20002108 "\240", recv=<optimized out>, recv@entry=0x20002108 "\240", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\240", send_len=1, recv=0x20002148 "", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xa2    0x00    0x00    0x00    0x1b    0x9e    0x13    0x08
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\242", recv=recv@entry=0x20002108 "\242", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\242", send_len=1, recv=0x20002148 "", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x20002108 "\242", recv=<optimized out>, recv@entry=0x20002108 "\242", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\242", send_len=1, recv=0x20002148 "", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xa4    0x00    0x00    0x00    0x55    0x55    0x55    0x55
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\244", recv=recv@entry=0x20002108 "\244", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\244\234\277UU", send_len=1, recv=0x20002148 "\234\277UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x20002108 "\244", recv=<optimized out>, recv@entry=0x20002108 "\244", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\244\234\277UU", send_len=1, recv=0x20002148 "\234\277UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xa6    0x00    0x00    0x00    0x55    0x55    0x55    0x55
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\246", recv=recv@entry=0x20002108 "\246", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\246\220<UU", send_len=1, recv=0x20002148 "\220<UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x20002108 "\246", recv=<optimized out>, recv@entry=0x20002108 "\246", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\246\220<UU", send_len=1, recv=0x20002148 "\220<UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xa8    0x00    0x00    0x00    0x55    0x55    0x55    0x55
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\250", recv=recv@entry=0x20002108 "\250", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\250[\025UU", send_len=1, recv=0x20002148 "[\025UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x20002108 "\250", recv=<optimized out>, recv@entry=0x20002108 "\250", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\250[\025UU", send_len=1, recv=0x20002148 "[\025UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xaa    0x00    0x00    0x00    0x55    0x55    0x55    0x55
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\252", recv=recv@entry=0x20002108 "\252", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\252Z\362UU", send_len=1, recv=0x20002148 "Z\362UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x20002108 "\252", recv=<optimized out>, recv@entry=0x20002108 "\252", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\252Z\362UU", send_len=1, recv=0x20002148 "Z\362UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xac    0x00    0x00    0x00    0x55    0x55    0x55    0x55
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\254", recv=recv@entry=0x20002108 "\254", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\254\202\270UU", send_len=1, recv=0x20002148 "\202\270UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x20002108 "\254", recv=<optimized out>, recv@entry=0x20002108 "\254", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\254\202\270UU", send_len=1, recv=0x20002148 "\202\270UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xae    0x00    0x00    0x20    0x55    0x55    0x55    0x55
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\256", recv=recv@entry=0x20002108 "\256", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\256n\230UU", send_len=1, recv=0x20002148 "n\230UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 recv=0x20002108
0x20002108:     0xae    0x00    0x00    0x20    0x55    0x55    0x55    0x55
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x20002108 "\256", recv=recv@entry=0x20002108 "\256", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\256n\230UU", send_len=1, recv=0x20002148 "n\230UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209
#3  0x0802305e in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#4  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#5  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#6  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#7  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x20002108 "\256", recv=<optimized out>, recv@entry=0x20002108 "\256", len=len@entry=3) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002147 "\256n\230UU", send_len=1, recv=0x20002148 "n\230UU", recv_len=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x08022f7a in AP_BoardConfig::check_ms5611 (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8156d78 "ms5611_ext") at ../../libraries/AP_BoardConfig/board_drivers.cpp:209

[probe] spi_check_register dev=mpu9250_ext reg=0x75 expected=0x71 read_flag=0x80
#0  AP_BoardConfig::spi_check_register (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8157030 "mpu9250_ext", regnum=regnum@entry=117 'u', value=value@entry=113 'q', read_flag=read_flag@entry=128 '\200') at ../../libraries/AP_BoardConfig/board_drivers.cpp:125
#1  0x08023072 in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#2  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#3  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#4  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#5  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi] get_device name=mpu9250_ext

[spi4] do_transfer dev=mpu9250_ext bus=2 device=1 len=2 send=0x200020f8 recv=0x200020f8
0x200020f8:     0xf5    0x00    0x00    0x00    0x55    0x55    0x55    0x55
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x200020f8 <incomplete sequence \365>, recv=recv@entry=0x200020f8 <incomplete sequence \365>, len=len@entry=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002137 "\365\215\216\023\b\\!", send_len=1, recv=0x2000215b "", recv_len=1) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x0803f5ba in AP_HAL::Device::read_registers (this=0x2001d400, first_reg=first_reg@entry=117 'u', recv=recv@entry=0x2000215b "", recv_len=recv_len@entry=1) at ../../libraries/AP_HAL/Device.cpp:187
#3  0x08022d62 in AP_BoardConfig::spi_check_register (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8157030 "mpu9250_ext", regnum=regnum@entry=117 'u', value=value@entry=113 'q', read_flag=read_flag@entry=128 '\200') at ../../libraries/AP_HAL/utility/OwnPtr.h:94
#4  0x08023072 in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#5  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#6  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#7  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x200020f8 <incomplete sequence \365>, recv=<optimized out>, recv@entry=0x200020f8 <incomplete sequence \365>, len=len@entry=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002137 "\365\215\216\023\b\\!", send_len=1, recv=0x2000215b "", recv_len=1) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x0803f5ba in AP_HAL::Device::read_registers (this=0x2001d400, first_reg=first_reg@entry=117 'u', recv=recv@entry=0x2000215b "", recv_len=recv_len@entry=1) at ../../libraries/AP_HAL/Device.cpp:187

[probe] spi_check_register dev=icm20602_ext reg=0x75 expected=0x12 read_flag=0x80
#0  AP_BoardConfig::spi_check_register (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8157058 "icm20602_ext", regnum=regnum@entry=117 'u', value=value@entry=18 '\022', read_flag=read_flag@entry=128 '\200') at ../../libraries/AP_BoardConfig/board_drivers.cpp:125
#1  0x08023084 in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#2  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#3  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#4  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#5  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405

[spi] get_device name=icm20602_ext

[spi4] do_transfer dev=icm20602_ext bus=2 device=4 len=2 send=0x200020f8 recv=0x200020f8
0x200020f8:     0xf5    0x00    0x00    0x00    0x55    0x55    0x55    0x55
#0  ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=send@entry=0x200020f8 <incomplete sequence \365>, recv=recv@entry=0x200020f8 <incomplete sequence \365>, len=len@entry=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:175
#1  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002137 "\365\215\216\023\b\\!", send_len=1, recv=0x2000215b "", recv_len=1) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#2  0x0803f5ba in AP_HAL::Device::read_registers (this=0x2001d400, first_reg=first_reg@entry=117 'u', recv=recv@entry=0x2000215b "", recv_len=recv_len@entry=1) at ../../libraries/AP_HAL/Device.cpp:187
#3  0x08022d62 in AP_BoardConfig::spi_check_register (this=this@entry=0x20002fd0 <rover+16>, devname=devname@entry=0x8157058 "icm20602_ext", regnum=regnum@entry=117 'u', value=value@entry=18 '\022', read_flag=read_flag@entry=128 '\200') at ../../libraries/AP_HAL/utility/OwnPtr.h:94
#4  0x08023084 in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:302
#5  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#6  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#7  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409

[spi4] spi_lld_start spip=0x20018bf8
#0  spi_lld_start (spip=spip@entry=0x20018bf8 <SPID4>) at ../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c:315
#1  0x0814d698 in spiStart (spip=0x20018bf8 <SPID4>, config=<optimized out>) at ../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc:104
#2  0x081398f6 in ChibiOS::SPIBus::start_peripheral (this=0x2001d2f8) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:378
#3  0x081399a8 in ChibiOS::SPIDevice::acquire_bus (this=0x2001d400, set=true, skip_cs=skip_cs@entry=false) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:429
#4  0x08139a38 in ChibiOS::SPIDevice::set_chip_select (this=<optimized out>, set=<optimized out>) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:442
#5  0x081394b6 in ChibiOS::SPIDevice::do_transfer (this=this@entry=0x2001d400, send=<optimized out>, send@entry=0x200020f8 <incomplete sequence \365>, recv=<optimized out>, recv@entry=0x200020f8 <incomplete sequence \365>, len=len@entry=2) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:178
#6  0x0813964c in ChibiOS::SPIDevice::transfer (this=0x2001d400, send=0x20002137 "\365\215\216\023\b\\!", send_len=1, recv=0x2000215b "", recv_len=1) at ../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp:307
#7  0x0803f5ba in AP_HAL::Device::read_registers (this=0x2001d400, first_reg=first_reg@entry=117 'u', recv=recv@entry=0x2000215b "", recv_len=recv_len@entry=1) at ../../libraries/AP_HAL/Device.cpp:187

[FATAL] throw_error called!
[FATAL] format string: Board Validation %s Failed
#0  AP_BoardConfig::throw_error (err_type=err_type@entry=0x8157244 "Config Error", fmt=0x8157120 "Board Validation %s Failed", fmt@entry=0x20002160 "", arg=..., arg@entry=...) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:461
#1  0x08023554 in AP_BoardConfig::config_error (fmt=0x8157120 "Board Validation %s Failed") at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:515
#2  0x080230dc in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:306
#3  0x080232aa in AP_BoardConfig::board_setup_drivers (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:82
#4  0x0802332a in AP_BoardConfig::board_setup (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:491
#5  0x080233c4 in AP_BoardConfig::init (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/AP_BoardConfig.cpp:409
#6  0x0807d986 in AP_Vehicle::setup (this=0x20002fc0 <rover>) at ../../libraries/AP_Vehicle/AP_Vehicle.cpp:405
#7  0x080e6ef6 in main_loop () at ../../libraries/AP_HAL_ChibiOS/HAL_ChibiOS_Class.cpp:256
#8  0x080e6fbc in HAL_ChibiOS::run (this=<optimized out>, argc=<optimized out>, argv=<optimized out>, callbacks=0x2000e0fc <hal_chibios>) at ../../libraries/AP_HAL_ChibiOS/HAL_ChibiOS_Class.cpp:354
#9  0x080144c8 in main (argc=<optimized out>, argv=<optimized out>) at ../../Rover/Rover.cpp:517
#2  0x080230dc in AP_BoardConfig::board_autodetect (this=this@entry=0x20002fd0 <rover+16>) at ../../libraries/AP_BoardConfig/board_drivers.cpp:306
306                 config_error("Board Validation %s Failed", errored_check);
[FATAL] AP_IOMCU::check_crc locals, if this is the IOMCU path:
gdbscripts/ardurover_script.gdb:192: Error in sourced command file:
No symbol "crc" in current context.
(gdb)

```