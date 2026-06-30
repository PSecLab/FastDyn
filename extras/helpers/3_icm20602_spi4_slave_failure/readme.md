



```bash
Script started on 2026-06-29 17:24:32-04:00 [TERM="xterm-256color" TTY="/dev/pts/12" COLUMNS="85" LINES="47"]
[34m0x08005300[m in [33mReset_Handler[m ()

[diag] FastDyn idle-starvation diagnostic script loaded
[m[diag] SPID1 = 0x20018c60
[m[diag] SPID4 = 0x20018bf8
[mBreakpoint 1 at [34m0x8023460[m: file [32m../../libraries/AP_BoardConfig/AP_BoardConfig.cpp[m, line 461.
[diag] installed breakpoint: AP_BoardConfig::throw_error
Breakpoint 2 at [34m0x80230d6[m: file [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m, line 306.
[diag] installed breakpoint: /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/libraries/AP_BoardConfig/board_drivers.cpp:306
Breakpoint 3 at [34m0x8022ef8[m: file [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m, line 182.
[diag] installed breakpoint: AP_BoardConfig::check_ms5611
Breakpoint 4 at [34m0x8022d04[m: file [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m, line 125.
[diag] installed breakpoint: AP_BoardConfig::spi_check_register
Breakpoint 5 at [34m0x8022d8c[m: file [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m, line 155.
[diag] installed breakpoint: AP_BoardConfig::spi_check_register_inv2
Breakpoint 6 at [34m0x803f58c[m: file [32m../../libraries/AP_HAL/Device.cpp[m, line 184.
[diag] installed breakpoint: AP_HAL::Device::read_registers
Breakpoint 7 at [34m0x813949c[m: file [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m, line 175.
[diag] installed breakpoint: ChibiOS::SPIDevice::do_transfer
Breakpoint 8 at [34m0x8139a3c[m: file [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m, line 450.
[diag] installed breakpoint: ChibiOS::SPIDeviceManager::get_device
Breakpoint 9 at [34m0x814b9bc[m: file [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m, line 315.
[diag] installed breakpoint: spi_lld_start
Breakpoint 10 at [34m0x814bd34[m: file [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m, line 658.
[diag] installed breakpoint: spi_lld_abort
[diag] breakpoints installed; continuing target

[spi] get_device name=ramtron
[reg-read] enter caller_dev=<unavailable> first_reg=0x9f recv=0x20002120 'U' <repeats 12 times> len=9
[return] AP_HAL::Device::read_registers caller_dev=<unavailable> first_reg=0x9f ret=true
[return] AP_HAL::Device::read_registers recv_bytes=7f 7f 7f 7f 7f c2 00 26 08

[probe] check_ms5611 enter dev=ms5611

[spi] get_device name=ms5611

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=1 send=0x816c808 <AP_BoardConfig::check_ms5611(char const*)::CMD_MS56XX_RESET> "\036" recv=0x0 pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=1e

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x816c808 <AP_BoardConfig::check_ms5611(char const*)::CMD_MS56XX_RESET> "\036", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x0, [36mlen=len@entry[m=1) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x816c808 <AP_BoardConfig::check_ms5611(char const*)::CMD_MS56XX_RESET> "\036", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x0, [36mlen=len@entry[m=1) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=<none>

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3 send=0x20002108 "\240" recv=0x20002108 "\240" pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=a0 00 00

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\240", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\240", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 00 00

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3 send=0x20002108 "\242" recv=0x20002108 "\242" pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=a2 00 00

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\242", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\242", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 9c bf

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3 send=0x20002108 "\244" recv=0x20002108 "\244" pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=a4 00 00

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\244", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\244", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 90 3c

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3 send=0x20002108 "\246" recv=0x20002108 "\246" pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=a6 00 00

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\246", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\246", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 5b 15

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3 send=0x20002108 "\250" recv=0x20002108 "\250" pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=a8 00 00

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\250", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\250", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 5a f2

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3 send=0x20002108 "\252" recv=0x20002108 "\252" pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=aa 00 00

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3 send=0x20002108 "\252" recv=0x20002108 "\252" pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=aa 00 00

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\252", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\252", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 82 b8
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 82 b8

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3 send=0x20002108 "\254" recv=0x20002108 "\254" pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=ac 00 00

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\254", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\254", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 6e 98

[spi1] do_transfer dev=ms5611 bus=0 device=3 len=3 send=0x20002108 "\256" recv=0x20002108 "\256" pal_line=0x40020c07 port_base=0x40020c00 port_idx=3 pad=7 signal_id=55
[spi1] tx_bytes=ae 00 00

[spi1] spi_lld_start spip=0x20018c60 <SPID1>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018c60 <SPID1>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018c60 <SPID1>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d1f0) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d2f8, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d2f8, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\256", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\256", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611 bus=spi1 device=3 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 00 00
[return] check_ms5611 dev=ms5611 ret=true

[probe] check_ms5611 enter dev=ms5611_ext

[spi] get_device name=ms5611_ext

[spi] get_device name=ms5611_ext

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=1 send=0x816c808 <AP_BoardConfig::check_ms5611(char const*)::CMD_MS56XX_RESET> "\036" recv=0x0 pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=1e

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x816c808 <AP_BoardConfig::check_ms5611(char const*)::CMD_MS56XX_RESET> "\036", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x0, [36mlen=len@entry[m=1) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=<none>

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 "\240" recv=0x20002108 "\240" pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=a0 00 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\240", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\240", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 00 00

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 "\242" recv=0x20002108 "\242" pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=a2 00 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\242", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\242", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 9c bf

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 "\244" recv=0x20002108 "\244" pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=a4 00 00

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 "\244" recv=0x20002108 "\244" pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=a4 00 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\244", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\244", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 90 3c
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 90 3c

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 "\246" recv=0x20002108 "\246" pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=a6 00 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\246", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\246", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 5b 15

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 "\250" recv=0x20002108 "\250" pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=a8 00 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\250", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\250", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 5a f2

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 "\252" recv=0x20002108 "\252" pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=aa 00 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\252", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\252", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 82 b8

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 "\254" recv=0x20002108 "\254" pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=ac 00 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\254", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\254", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 6e 98

[spi4] do_transfer dev=ms5611_ext bus=2 device=2 len=3 send=0x20002108 "\256" recv=0x20002108 "\256" pal_line=0x4002080e port_base=0x40020800 port_idx=2 pad=14 signal_id=46
[spi4] tx_bytes=ae 00 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x20002108 "\256", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x20002108 "\256", [36mlen=len@entry[m=3) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=ms5611_ext bus=spi4 device=2 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=00 00 00
[return] check_ms5611 dev=ms5611_ext ret=true

[probe] spi_check_register enter dev=mpu9250_ext reg=0x75 expected=0x71 read_flag=0x80

[spi] get_device name=mpu9250_ext
[reg-read] enter caller_dev=mpu9250_ext first_reg=0x75 recv=0x2000215b "" len=1

[spi4] do_transfer dev=mpu9250_ext bus=2 device=1 len=2 send=0x200020f8 <incomplete sequence \365> recv=0x200020f8 <incomplete sequence \365> pal_line=0x40021004 port_base=0x40021000 port_idx=4 pad=4 signal_id=68
[spi4] tx_bytes=f5 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x200020f8 <incomplete sequence \365>, [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x200020f8 <incomplete sequence \365>, [36mlen=len@entry[m=2) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x200020f8 <incomplete sequence \365>, [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x200020f8 <incomplete sequence \365>, [36mlen=len@entry[m=2) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=mpu9250_ext bus=spi4 device=1 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=ff 12
[return] AP_HAL::Device::read_registers caller_dev=mpu9250_ext first_reg=0x75 ret=true
[return] AP_HAL::Device::read_registers recv_bytes=12
[return] spi_check_register dev=mpu9250_ext reg=0x75 expected=0x71 ret=false

[probe] spi_check_register enter dev=icm20602_ext reg=0x75 expected=0x12 read_flag=0x80

[spi] get_device name=icm20602_ext

[spi] get_device name=icm20602_ext
[reg-read] enter caller_dev=icm20602_ext first_reg=0x75 recv=0x2000215b "U\374\322\001 " len=1

[spi4] do_transfer dev=icm20602_ext bus=2 device=4 len=2 send=0x200020f8 <incomplete sequence \365> recv=0x200020f8 <incomplete sequence \365> pal_line=0x4002080d port_base=0x40020800 port_idx=2 pad=13 signal_id=45
[spi4] tx_bytes=f5 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x200020f8 <incomplete sequence \365>, [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x200020f8 <incomplete sequence \365>, [36mlen=len@entry[m=2) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=icm20602_ext bus=spi4 device=4 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=ff 12
[return] AP_HAL::Device::read_registers caller_dev=icm20602_ext first_reg=0x75 ret=true
[return] AP_HAL::Device::read_registers recv_bytes=12
[return] spi_check_register dev=icm20602_ext reg=0x75 expected=0x12 ret=true

[probe] spi_check_register enter dev=lsm9ds0_ext_g reg=0x0f expected=0xd4 read_flag=0x80

[spi] get_device name=lsm9ds0_ext_g
[reg-read] enter caller_dev=lsm9ds0_ext_g first_reg=0x0f recv=0x2000215b "\022\374\322\001 " len=1

[spi4] do_transfer dev=lsm9ds0_ext_g bus=2 device=4 len=2 send=0x200020f8 "\217" recv=0x200020f8 "\217" pal_line=0x4002080d port_base=0x40020800 port_idx=2 pad=13 signal_id=45
[spi4] tx_bytes=8f 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x200020f8 "\217", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x200020f8 "\217", [36mlen=len@entry[m=2) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=lsm9ds0_ext_g bus=spi4 device=4 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=ff 00
[return] AP_HAL::Device::read_registers caller_dev=lsm9ds0_ext_g first_reg=0x0f ret=true
[return] AP_HAL::Device::read_registers recv_bytes=00
[return] spi_check_register dev=lsm9ds0_ext_g reg=0x0f expected=0xd4 ret=false

[probe] spi_check_register enter dev=icm20948_ext reg=0x00 expected=0xea read_flag=0x80

[spi] get_device name=icm20948_ext
[reg-read] enter caller_dev=icm20948_ext first_reg=0x00 recv=0x2000215b "" len=1

[spi4] do_transfer dev=icm20948_ext bus=2 device=1 len=2 send=0x200020f8 "\200" recv=0x200020f8 "\200" pal_line=0x40021004 port_base=0x40021000 port_idx=4 pad=4 signal_id=68
[spi4] tx_bytes=80 00

[spi4] spi_lld_start spip=0x20018bf8 <SPID4>
#0  [33mspi_lld_start[m ([36mspip=spip@entry[m=0x20018bf8 <SPID4>) at [32m../../modules/ChibiOS/os/hal/ports/STM32/LLD/SPIv1/hal_spi_lld.c[m:315
#1  [34m0x0814d698[m in [33mspiStart[m ([36mspip[m=0x20018bf8 <SPID4>, [36mconfig[m=<optimized out>) at [32m../../modules/ChibiOS/os/hal/src/hal_spi_v1.inc[m:104
#2  [34m0x081398f6[m in [33mChibiOS::SPIBus::start_peripheral[m ([36mthis[m=0x2001d2f8) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:378
#3  [34m0x081399a8[m in [33mChibiOS::SPIDevice::acquire_bus[m ([36mthis[m=0x2001d400, [36mset[m=true, [36mskip_cs=skip_cs@entry[m=false) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:429
#4  [34m0x08139a38[m in [33mChibiOS::SPIDevice::set_chip_select[m ([36mthis[m=<optimized out>, [36mset[m=<optimized out>) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:442
#5  [34m0x081394b6[m in [33mChibiOS::SPIDevice::do_transfer[m ([36mthis=this@entry[m=0x2001d400, [36msend[m=<optimized out>, [36msend@entry[m=0x200020f8 "\200", [36mrecv[m=<optimized out>, [36mrecv@entry[m=0x200020f8 "\200", [36mlen=len@entry[m=2) at [32m../../libraries/AP_HAL_ChibiOS/SPIDevice.cpp[m:178
[return] ChibiOS::SPIDevice::do_transfer dev=icm20948_ext bus=spi4 device=1 ret=true
[return] ChibiOS::SPIDevice::do_transfer recv_bytes=ff 00
[return] AP_HAL::Device::read_registers caller_dev=icm20948_ext first_reg=0x00 ret=true
[return] AP_HAL::Device::read_registers recv_bytes=00
[return] spi_check_register dev=icm20948_ext reg=0x00 expected=0xea ret=false

[board] HAL_VALIDATE_BOARD failure candidate errored_check=<unavailable:value has been optimized out>
#0  [33mAP_BoardConfig::board_autodetect[m ([36mthis=this@entry[m=0x20002fd0 <rover+16>) at [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m:306
#1  [34m0x080232aa[m in [33mAP_BoardConfig::board_setup_drivers[m ([36mthis=this@entry[m=0x20002fd0 <rover+16>) at [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m:82
#2  [34m0x0802332a[m in [33mAP_BoardConfig::board_setup[m ([36mthis=this@entry[m=0x20002fd0 <rover+16>) at [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m:491
#3  [34m0x080233c4[m in [33mAP_BoardConfig::init[m ([36mthis=this@entry[m=0x20002fd0 <rover+16>) at [32m../../libraries/AP_BoardConfig/AP_BoardConfig.cpp[m:409
#4  [34m0x0807d986[m in [33mAP_Vehicle::setup[m ([36mthis[m=0x20002fc0 <rover>) at [32m../../libraries/AP_Vehicle/AP_Vehicle.cpp[m:405
#5  [34m0x080e6ef6[m in [33mmain_loop[m () at [32m../../libraries/AP_HAL_ChibiOS/HAL_ChibiOS_Class.cpp[m:256
#6  [34m0x080e6fbc[m in [33mHAL_ChibiOS::run[m ([36mthis[m=<optimized out>, [36margc[m=<optimized out>, [36margv[m=<optimized out>, [36mcallbacks[m=0x2000e0fc <hal_chibios>) at [32m../../libraries/AP_HAL_ChibiOS/HAL_ChibiOS_Class.cpp[m:354
#7  [34m0x080144c8[m in [33mmain[m ([36margc[m=<optimized out>, [36margv[m=<optimized out>) at [32m../../Rover/Rover.cpp[m:517

[FATAL] AP_BoardConfig::throw_error
[FATAL] err_type=Config Error fmt=Board Validation %s Failed
#0  [33mAP_BoardConfig::throw_error[m ([36merr_type=err_type@entry[m=0x8157244 "Config Error", [36mfmt[m=0x8157120 "Board Validation %s Failed", [36mfmt@entry[m=0x200162b0 <_monitor_thread_wa+1456> "\360r\002 \240\235\001 \267", [36marg[m=..., [36marg@entry[m=...) at [32m../../libraries/AP_BoardConfig/AP_BoardConfig.cpp[m:461
#1  [34m0x08023554[m in [33mAP_BoardConfig::config_error[m ([36mfmt[m=0x8157120 "Board Validation %s Failed") at [32m../../libraries/AP_BoardConfig/AP_BoardConfig.cpp[m:515
#2  [34m0x080230dc[m in [33mAP_BoardConfig::board_autodetect[m ([36mthis=this@entry[m=0x20002fd0 <rover+16>) at [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m:306
#3  [34m0x080232aa[m in [33mAP_BoardConfig::board_setup_drivers[m ([36mthis=this@entry[m=0x20002fd0 <rover+16>) at [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m:82
#4  [34m0x0802332a[m in [33mAP_BoardConfig::board_setup[m ([36mthis=this@entry[m=0x20002fd0 <rover+16>) at [32m../../libraries/AP_BoardConfig/board_drivers.cpp[m:491
#5  [34m0x080233c4[m in [33mAP_BoardConfig::init[m ([36mthis=this@entry[m=0x20002fd0 <rover+16>) at [32m../../libraries/AP_BoardConfig/AP_BoardConfig.cpp[m:409
#6  [34m0x0807d986[m in [33mAP_Vehicle::setup[m ([36mthis[m=0x20002fc0 <rover>) at [32m../../libraries/AP_Vehicle/AP_Vehicle.cpp[m:405
#7  [34m0x080e6ef6[m in [33mmain_loop[m () at [32m../../libraries/AP_HAL_ChibiOS/HAL_ChibiOS_Class.cpp[m:256
#8  [34m0x080e6fbc[m in [33mHAL_ChibiOS::run[m ([36mthis[m=<optimized out>, [36margc[m=<optimized out>, [36margv[m=<optimized out>, [36mcallbacks[m=0x2000e0fc <hal_chibios>) at [32m../../libraries/AP_HAL_ChibiOS/HAL_ChibiOS_Class.cpp[m:354
#9  [34m0x080144c8[m in [33mmain[m ([36margc[m=<optimized out>, [36margv[m=<optimized out>) at [32m../../Rover/Rover.cpp[m:517
[FATAL] stopped for inspection

Breakpoint 1, [33mAP_BoardConfig::throw_error[m ([36merr_type=err_type@entry[m=0x8157244 "Config Error", [36mfmt[m=0x8157120 "Board Validation %s Failed", [36mfmt@entry[m=0x200162b0 <_monitor_thread_wa+1456> "\360r\002 \240\235\001 \267", [36marg[m=..., [36marg@entry[m=...) at [32m../../libraries/AP_BoardConfig/AP_BoardConfig.cpp[m:461
461	[31m{[m
[?2004h(gdb)
[?2004l
[?2004h(gdb) q
[?2004l
Detaching from program: /scratch/Fastdyn/ardurover_rehosting_fastdyn/clean_rehosting/FastDyn/virtuals/physics/flight_controllers/courbet/bin/ardurover_v462, process 1
Remote connection closed

Script done on 2026-06-29 17:26:11-04:00 [COMMAND_EXIT_CODE="0"]
```