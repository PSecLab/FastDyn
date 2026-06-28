set pagination off
set confirm off
set print thread-events off

file virtuals/physics/flight_controllers/courbet/bin/ardurover_v462

directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot
directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/build/CubeBlack

target remote localhost:1234

set $spid1 = &SPID1
set $spid4 = &SPID4
set $probe_events = 0
set $spi1_events = 0
set $spi4_events = 0
set $idle_armed = 0
set $iomcu_crc_reads = 0

printf "\nSPID1 = %p\n", $spid1
printf "SPID4 = %p\n", $spid4

# ---------------------------------------------------------------
# BP1: throw_error — the execution trace shows this is called
# right before the idle loop. Captures the actual error message.
# ---------------------------------------------------------------
break AP_BoardConfig::throw_error
commands
  silent
  printf "\n[FATAL] throw_error called!\n"
  printf "[FATAL] format string: %s\n", fmt
  bt 12
  frame 2
  printf "[FATAL] AP_IOMCU::check_crc locals, if this is the IOMCU path:\n"
  p/x crc
  p/x io_crc
  # Don't continue — let it stop so we can inspect
end

# ---------------------------------------------------------------
# BP2: check_ms5611 — should now pass with our ms5611_spi.c slave.
# Kept to confirm SPI1 baro probe succeeds this time.
# ---------------------------------------------------------------
break AP_BoardConfig::check_ms5611
commands
  silent
  set $probe_events = $probe_events + 1
  printf "\n[probe] check_ms5611 dev=%s\n", devname
  bt 6
  continue
end

# ---------------------------------------------------------------
# BP3-4: spi_check_register — generic SPI probe functions.
# Every sensor probe goes through these. Tells us which device
# name, register, and expected value are being checked.
# ---------------------------------------------------------------
break AP_BoardConfig::spi_check_register
commands
  silent
  set $probe_events = $probe_events + 1
  printf "\n[probe] spi_check_register dev=%s reg=0x%02x expected=0x%02x read_flag=0x%02x\n", devname, regnum, value, read_flag
  bt 6
  continue
end

break AP_BoardConfig::spi_check_register_inv2
commands
  silent
  set $probe_events = $probe_events + 1
  printf "\n[probe] spi_check_register_inv2 dev=%s reg=0x%02x expected=0x%02x read_flag=0x%02x\n", devname, regnum, value, read_flag
  bt 6
  continue
end

# ---------------------------------------------------------------
# BP5: get_device — SPI device allocation. Shows the name of
# every SPI device the firmware requests, in order.
# ---------------------------------------------------------------
break ChibiOS::SPIDeviceManager::get_device
commands
  silent
  printf "\n[spi] get_device name=%s\n", name
  continue
end

# ---------------------------------------------------------------
# BP6: do_transfer — filtered for SPI1 (bus==0) and SPI4 (bus==2).
# CubeBlack maps: SPI1=bus0, SPI2=bus1, SPI4=bus2.
# Arms the idle breakpoint after we see SPI4 activity.
# ---------------------------------------------------------------
break ChibiOS::SPIDevice::do_transfer
commands
  silent
  if this->device_desc.bus == 0
    set $spi1_events = $spi1_events + 1
    printf "\n[spi1] do_transfer dev=%s bus=%u device=%u len=%u\n", this->device_desc.name, this->device_desc.bus, this->device_desc.device, len
    if send != 0
      x/8xb send
    end
  end
  if this->device_desc.bus == 2
    set $spi4_events = $spi4_events + 1
    printf "\n[spi4] do_transfer dev=%s bus=%u device=%u len=%u send=%p recv=%p\n", this->device_desc.name, this->device_desc.bus, this->device_desc.device, len, send, recv
    if send != 0
      x/8xb send
    end
    bt 8
  end
  continue
end

# ---------------------------------------------------------------
# BP7-8: Invensense hardware init — hwdef.dat shows icm20948_ext
# (Invensensev2) and icm20602_ext (Invensense) on SPI4.
# These are the IMU driver init functions that will fail if the
# SPI4 slave doesn't respond.
# ---------------------------------------------------------------
break AP_InertialSensor_Invensense::_hardware_init
commands
  silent
  printf "\n[imu] Invensense::_hardware_init entered\n"
  bt 8
  continue
end

break AP_InertialSensor_Invensensev2::_hardware_init
commands
  silent
  printf "\n[imu] Invensensev2::_hardware_init entered\n"
  bt 8
  continue
end

# ---------------------------------------------------------------
# BP9-10: Invensense _check_whoami — the first thing _hardware_init
# does is read the WHO_AM_I register. If this fails, the driver
# gives up. This tells us exactly what value was read back.
# ---------------------------------------------------------------
break AP_InertialSensor_Invensense::_check_whoami
commands
  silent
  printf "\n[imu] Invensense::_check_whoami entered\n"
  bt 6
  continue
end

break AP_InertialSensor_Invensensev2::_check_whoami
commands
  silent
  printf "\n[imu] Invensensev2::_check_whoami entered\n"
  bt 6
  continue
end

# ---------------------------------------------------------------
# BP11: LSM303D hardware init — hwdef.dat shows lsm9ds0_ext_am
# (LSM303D compass) is also probed during board_autodetect.
# ---------------------------------------------------------------
break AP_Compass_LSM303D::_hardware_init
commands
  silent
  printf "\n[compass] LSM303D::_hardware_init entered\n"
  bt 8
  continue
end

# ---------------------------------------------------------------
# BP12: spi_lld_start filtered for SPID4 — confirms whether
# the SPI4 hardware peripheral was actually started.
# ---------------------------------------------------------------
break spi_lld_start if spip == $spid4
commands
  silent
  set $spi4_events = $spi4_events + 1
  printf "\n[spi4] spi_lld_start spip=%p\n", spip
  bt 8
  continue
end

# ---------------------------------------------------------------
# BP13: spi_lld_abort — catches DMA timeouts on ANY SPI bus
# (e.g. if the ms5611 reset command or an SPI4 probe times out).
# ---------------------------------------------------------------
break spi_lld_abort
commands
  silent
  printf "\n[spi] spi_lld_abort spip=%p probe_events=%d spi1_events=%d spi4_events=%d\n", spip, $probe_events, $spi1_events, $spi4_events
  bt 12
end

continue
