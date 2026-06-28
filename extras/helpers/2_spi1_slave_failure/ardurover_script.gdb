set pagination off
set confirm off
set print thread-events off

file virtuals/physics/flight_controllers/courbet/bin/ardurover_v462

directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot
directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/build/CubeBlack

target remote localhost:1234

set $spid1 = &SPID1
set $probe_events = 0
set $spi1_events = 0
set $idle_armed = 0

printf "\nSPID1 = %p\n", $spid1

break AP_BoardConfig::check_ms5611
commands
  silent
  set $probe_events = $probe_events + 1
  printf "\n[probe] check_ms5611 dev=%s\n", devname
  bt 6
  continue
end

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

break ChibiOS::SPIDeviceManager::get_device
commands
  silent
  printf "\n[spi] get_device name=%s\n", name
  continue
end

break ChibiOS::SPIDevice::do_transfer
commands
  silent
  if this->device_desc.bus == 0
    set $spi1_events = $spi1_events + 1
    printf "\n[spi1] do_transfer dev=%s bus=%u device=%u len=%u send=%p recv=%p\n", this->device_desc.name, this->device_desc.bus, this->device_desc.device, len, send, recv
    if send != 0
      x/16xb send
    end
    bt 8
    if $idle_armed == 0
      set $idle_armed = 1
      enable $idle_bp
      ignore $idle_bp 2000
      printf "[idle] armed after SPI1 transfer; ignoring first 2000 idle hits\n"
    end
  end
  continue
end

break spi_lld_start if spip == $spid1
commands
  silent
  set $spi1_events = $spi1_events + 1
  printf "\n[spi1] spi_lld_start spip=%p\n", spip
  bt 8
  if $idle_armed == 0
    set $idle_armed = 1
    enable $idle_bp
    ignore $idle_bp 2000
    printf "[idle] armed after SPI1 start; ignoring first 2000 idle hits\n"
  end
  continue
end

break spi_lld_exchange if spip == $spid1
commands
  silent
  set $spi1_events = $spi1_events + 1
  printf "\n[spi1] spi_lld_exchange n=%u tx=%p rx=%p\n", n, txbuf, rxbuf
  if txbuf != 0
    x/16xb txbuf
  end
  bt 10
  continue
end

break spi_lld_send if spip == $spid1
commands
  silent
  set $spi1_events = $spi1_events + 1
  printf "\n[spi1] spi_lld_send n=%u tx=%p\n", n, txbuf
  if txbuf != 0
    x/16xb txbuf
  end
  bt 10
  continue
end

break spi_lld_receive if spip == $spid1
commands
  silent
  set $spi1_events = $spi1_events + 1
  printf "\n[spi1] spi_lld_receive n=%u rx=%p\n", n, rxbuf
  bt 10
  continue
end

break spi_lld_abort if spip == $spid1
commands
  silent
  printf "\n[spi1] spi_lld_abort spip=%p probe_events=%d spi1_events=%d\n", spip, $probe_events, $spi1_events
  bt 12
end

break __idle_thread
set $idle_bp = $bpnum
commands
  silent
  printf "\n[idle] __idle_thread after SPI/probe activity: probe_events=%d spi1_events=%d\n", $probe_events, $spi1_events
  bt 20
end
disable $idle_bp

continue
