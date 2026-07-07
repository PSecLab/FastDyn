set pagination off
set confirm off
set print thread-events off
set style enabled off
set breakpoint pending on

# Sparse ArduRover 4.6.2 progress probe.
#
# This script intentionally avoids RTOS primitives and high-frequency sensor
# callbacks. It only asks whether the main setup path resumes after the
# Invensense backend registers its DeviceBus periodic callback.

file virtuals/physics/flight_controllers/courbet/bin/ardurover_v462

directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot
directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/build/CubeBlack

target remote localhost:1234

set $post_cb = 0
set $post_dtor = 0
set $start_ret = 0
set $after_backend = 0
set $init_gyro = 0
set $scheduler_loop = 0

printf "\n[sparse] loaded low-perturbation progress probe\n"
printf "[sparse] no RTOS/semaphore/FIFO/frontend breakpoints are installed\n"

# AP_InertialSensor_Invensense::start(), immediately after
# register_periodic_callback().
thbreak *0x0811a878
commands
  silent
  set $post_cb = 1
  printf "\n[SPARSE] post_periodic_callback pc=%p lr=%p sp=%p\n", $pc, $lr, $sp
  continue
end

# AP_InertialSensor_Invensense::start(), immediately after
# WithSemaphore::~WithSemaphore() returns.
thbreak *0x0811a87e
commands
  silent
  set $post_dtor = 1
  printf "\n[SPARSE] post_WithSemaphore_dtor pc=%p lr=%p sp=%p\n", $pc, $lr, $sp
  continue
end

# AP_InertialSensor_Invensense::start() common return path.
thbreak *0x0811a4e6
commands
  silent
  set $start_ret = 1
  printf "\n[SPARSE] Invensense_start_return pc=%p lr=%p\n", $pc, $lr
  continue
end

# AP_InertialSensor::_start_backends(), after one backend->start().
thbreak *0x080436d2
commands
  silent
  set $after_backend = 1
  printf "\n[SPARSE] backend_start_returned pc=%p lr=%p\n", $pc, $lr
  continue
end

# AP_InertialSensor::init(), returned from _start_backends.
thbreak *0x080437da
commands
  silent
  printf "\n[SPARSE] inertial_init_after_start_backends pc=%p lr=%p\n", $pc, $lr
  continue
end

# AP_InertialSensor::init_gyro().
thbreak *0x08042a5c
commands
  silent
  set $init_gyro = 1
  printf "\n[SPARSE] init_gyro pc=%p lr=%p\n", $pc, $lr
  continue
end

# AP_InertialSensor::wait_for_sample().
thbreak *0x08041d08
commands
  silent
  printf "\n[SPARSE] wait_for_sample pc=%p lr=%p\n", $pc, $lr
  continue
end

# AP_Scheduler::loop().
thbreak *0x0807c278
commands
  silent
  set $scheduler_loop = 1
  printf "\n[SPARSE] scheduler_loop pc=%p lr=%p\n", $pc, $lr
  continue
end

printf "[sparse] breakpoints installed; continuing target\n"
continue
