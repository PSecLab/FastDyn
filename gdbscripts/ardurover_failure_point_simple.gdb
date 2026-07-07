set pagination off
set confirm off
set print thread-events off
set style enabled off
set breakpoint pending on

# Minimal ArduRover 4.6.2 milestone probe.
# Use hardware breakpoints: software breakpoints did not fire reliably in the
# previous run. Avoid high-frequency RTOS/timer breakpoints because they
# perturb scheduling and can create misleading early stops.

file virtuals/physics/flight_controllers/courbet/bin/ardurover_v462

directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot
directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/build/CubeBlack

target remote localhost:1234

set $setup_seen = 0
set $rover_init_seen = 0
set $startup_ins_seen = 0
set $imu_accum_hits = 0
set $inv_cleanup_seen = 0
set $reg_gyro_hits = 0
set $reg_accel_hits = 0
set $fifo_reset_hits = 0
set $fast_fifo_reset_hits = 0
set $notify_gyro_hits = 0
set $notify_accel_hits = 0

printf "\n[simple] loaded hardware-breakpoint milestone probe\n"
printf "[simple] low-perturbation mode: no generic RTOS semaphore breakpoints\n"
printf "[simple] keep breakpoint order stable: explicit disable numbers are used for high-frequency breakpoints\n"

# HAL_ChibiOS::run(int, char* const*, AP_HAL::HAL::Callbacks*) const
hbreak *0x080e6fac
commands
  silent
  printf "\n[MILESTONE] HAL_ChibiOS::run pc=%p lr=%p\n", $pc, $lr
  bt 5
  continue
end

# ChibiOS::Scheduler::init()
hbreak *0x08139ca0
commands
  silent
  printf "\n[MILESTONE] ChibiOS::Scheduler::init pc=%p lr=%p\n", $pc, $lr
  bt 5
  continue
end

# AP_Vehicle::setup()
hbreak *0x0807d86c
commands
  silent
  set $setup_seen = 1
  printf "\n[MILESTONE] AP_Vehicle::setup pc=%p lr=%p\n", $pc, $lr
  bt 8
  continue
end

# Rover::init_ardupilot()
hbreak *0x0801acd4
commands
  silent
  set $rover_init_seen = 1
  printf "\n[MILESTONE] Rover::init_ardupilot pc=%p lr=%p\n", $pc, $lr
  bt 8
  continue
end

# Rover::startup_INS()
hbreak *0x0801aaa8
commands
  silent
  set $startup_ins_seen = 1
  printf "\n[MILESTONE] Rover::startup_INS pc=%p lr=%p\n", $pc, $lr
  bt 8
  continue
end

# AP_InertialSensor::init(unsigned short)
hbreak *0x08043720
commands
  silent
  printf "\n[MILESTONE] AP_InertialSensor::init this=%p loop_rate=%u pc=%p\n", $r0, $r1, $pc
  bt 8
  continue
end

# AP_InertialSensor::register_gyro(uint8_t&, uint16_t, uint32_t)
hbreak *0x08041434
commands
  silent
  set $reg_gyro_hits = $reg_gyro_hits + 1
  printf "\n[REG] AP_InertialSensor::register_gyro hit=%d this=%p instance_ref=%p rate=%u id=0x%x pc=%p\n", $reg_gyro_hits, $r0, $r1, $r2, $r3, $pc
  bt 8
  continue
end

# AP_InertialSensor::register_accel(uint8_t&, uint16_t, uint32_t)
hbreak *0x0804150c
commands
  silent
  set $reg_accel_hits = $reg_accel_hits + 1
  printf "\n[REG] AP_InertialSensor::register_accel hit=%d this=%p instance_ref=%p rate=%u id=0x%x pc=%p\n", $reg_accel_hits, $r0, $r1, $r2, $r3, $pc
  bt 8
  continue
end

# AP_InertialSensor_Invensense::start()
hbreak *0x0811a4d0
commands
  silent
  printf "\n[MILESTONE] old Invensense::start this=%p pc=%p\n", $r0, $pc
  bt 6
  continue
end

# AP_InertialSensor_Invensense::_fifo_reset(bool)
hbreak *0x08119aec
commands
  silent
  set $fifo_reset_hits = $fifo_reset_hits + 1
  if $fifo_reset_hits <= 8
    printf "\n[FIFO] old Invensense::_fifo_reset hit=%d this=%p full_reset=%u pc=%p\n", $fifo_reset_hits, $r0, $r1, $pc
  end
  if $fifo_reset_hits >= 1
    disable 10
  end
  continue
end

# AP_InertialSensor_Invensense::_fast_fifo_reset()
hbreak *0x08119ab8
commands
  silent
  set $fast_fifo_reset_hits = $fast_fifo_reset_hits + 1
  if $fast_fifo_reset_hits <= 8
    printf "\n[FIFO] old Invensense::_fast_fifo_reset hit=%d this=%p pc=%p\n", $fast_fifo_reset_hits, $r0, $pc
  end
  if $fast_fifo_reset_hits >= 1
    disable 11
  end
  continue
end

# AP_InertialSensor_Invensense::_accumulate_sensor_rate_sampling(uint8_t*, uint8_t)
hbreak *0x08119db0
commands
  silent
  set $imu_accum_hits = $imu_accum_hits + 1
  if $imu_accum_hits <= 8
    printf "\n[IMU] old Invensense accumulate hit=%d samples=%p n_samples=%u pc=%p\n", $imu_accum_hits, $r1, ($r2 & 0xff), $pc
    if $r1 != 0
      x/16xb $r1
    end
  end
  if $imu_accum_hits >= 1
    disable 12
  end
  continue
end

# AP_InertialSensor_Backend::_notify_new_gyro_raw_sample(uint8_t, Vector3f const&, uint64_t)
hbreak *0x08143014
commands
  silent
  set $notify_gyro_hits = $notify_gyro_hits + 1
  if $notify_gyro_hits <= 8
    printf "\n[FRONTEND] notify gyro raw hit=%d backend=%p instance=%u sample=%p time_lo=0x%x pc=%p\n", $notify_gyro_hits, $r0, ($r1 & 0xff), $r2, $r3, $pc
    x/3fw $r2
  end
  if $notify_gyro_hits >= 1
    disable 13
  end
  continue
end

# AP_InertialSensor_Backend::_notify_new_accel_raw_sample(uint8_t, Vector3f const&, uint64_t, bool)
hbreak *0x08142a70
commands
  silent
  set $notify_accel_hits = $notify_accel_hits + 1
  if $notify_accel_hits <= 8
    printf "\n[FRONTEND] notify accel raw hit=%d backend=%p instance=%u sample=%p time_lo=0x%x pc=%p\n", $notify_accel_hits, $r0, ($r1 & 0xff), $r2, $r3, $pc
    x/3fw $r2
  end
  if $notify_accel_hits >= 1
    disable 14
  end
  continue
end

# AP_InertialSensor_Invensense::start(), after register_periodic_callback().
thbreak *0x0811a878
commands
  silent
  set $inv_cleanup_seen = 1
  printf "\n[MILESTONE] old Invensense::start reached post-periodic-callback pc=%p lr=%p\n", $pc, $lr
  bt 8
  continue
end

# AP_InertialSensor_Invensense::start(), call WithSemaphore::~WithSemaphore().
thbreak *0x0811a87a
commands
  silent
  printf "\n[START-CLEANUP] old Invensense::start about to call WithSemaphore dtor pc=%p lr=%p sp=%p\n", $pc, $lr, $sp
  x/8wx $sp
  bt 8
  continue
end

# AP_InertialSensor_Invensense::start(), immediately after destructor returns.
thbreak *0x0811a87e
commands
  silent
  printf "\n[START-CLEANUP] old Invensense::start returned from WithSemaphore dtor pc=%p lr=%p sp=%p\n", $pc, $lr, $sp
  bt 8
  continue
end

# AP_InertialSensor_Invensense::start() common return path.
thbreak *0x0811a4e6
commands
  silent
  printf "\n[MILESTONE] old Invensense::start returning pc=%p lr=%p\n", $pc, $lr
  bt 8
  continue
end

# AP_InertialSensor::_start_backends(), immediately after one backend->start().
thbreak *0x080436d2
commands
  silent
  printf "\n[MILESTONE] _start_backends backend->start returned index=%u ins=%p pc=%p\n", $r4, $r5, $pc
  bt 8
  continue
end

# AP_InertialSensor::_start_backends(), backend loop complete.
thbreak *0x080436de
commands
  silent
  printf "\n[MILESTONE] _start_backends loop complete ins=%p pc=%p counts at +0x268:\n", $r5, $pc
  x/4xb $r5+0x268
  bt 8
  continue
end

# AP_InertialSensor::init(), returned from _start_backends and looping back to
# gyro calibration decision.
thbreak *0x080437da
commands
  silent
  printf "\n[MILESTONE] AP_InertialSensor::init returned from _start_backends pc=%p\n", $pc
  bt 8
  continue
end

# AP_InertialSensor::init_gyro()
thbreak *0x08042a5c
commands
  silent
  printf "\n[FORWARD] AP_InertialSensor::init_gyro reached this=%p pc=%p\n", $r0, $pc
  bt 12
  continue
end

# AP_InertialSensor::_init_gyro()
thbreak *0x080424a8
commands
  silent
  printf "\n[FORWARD] AP_InertialSensor::_init_gyro reached this=%p pc=%p\n", $r0, $pc
  bt 12
  continue
end

# AP_InertialSensor::wait_for_sample()
thbreak *0x08041d08
commands
  silent
  printf "\n[FORWARD] AP_InertialSensor::wait_for_sample reached this=%p pc=%p\n", $r0, $pc
  bt 12
  continue
end

# AP_Scheduler::run(unsigned long)
thbreak *0x0807bd14
commands
  silent
  printf "\n[SUCCESS] AP_Scheduler::run reached this=%p time_available=%u pc=%p\n", $r0, $r1, $pc
  bt 12
  continue
end

# AP_Scheduler::loop()
thbreak *0x0807c278
commands
  silent
  printf "\n[SUCCESS] AP_Scheduler::loop reached this=%p pc=%p\n", $r0, $pc
  bt 12
  continue
end

printf "[simple] breakpoints installed; continuing target\n"
continue
