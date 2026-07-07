set pagination off
set confirm off
set print thread-events off
set style enabled off
set breakpoint pending on

file virtuals/physics/flight_controllers/courbet/bin/ardurover_v462
directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot
directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/build/CubeBlack
target remote localhost:1234

# after target remote ...

define smallbt
  printf "pc=%p lr=%p sp=%p xpsr=0x%x\n", $pc, $lr, $sp, $xpsr
  bt 10
end

hbreak *0x0811a878
commands 1
  silent
  printf "\n[START] post periodic callback registration\n"
  smallbt
  disable 1
  continue
end

hbreak *0x0811a87a
commands 2
  silent
  printf "\n[START] before WithSemaphore dtor\n"
  smallbt
  disable 2
  continue
end

hbreak *0x0811a87e
commands 3
  silent
  printf "\n[GOOD] returned from WithSemaphore dtor\n"
  smallbt
  disable 3
  continue
end

hbreak *0x0811a4e6
commands 4
  silent
  printf "\n[GOOD] Invensense::start returning\n"
  smallbt
  disable 4
  continue
end

hbreak *0x080436d2
commands 5
  silent
  printf "\n[GOOD] backend->start returned into _start_backends\n"
  smallbt
  disable 5
  continue
end

hbreak *0x080437da
commands 6
  silent
  printf "\n[GOOD] AP_InertialSensor::init after _start_backends\n"
  smallbt
  disable 6
  continue
end

hbreak AP_InertialSensor::init_gyro
commands 7
  silent
  printf "\n[GOOD] init_gyro reached\n"
  smallbt
  disable 7
  continue
end

hbreak AP_InertialSensor::_init_gyro
commands 8
  silent
  printf "\n[GOOD] _init_gyro reached\n"
  smallbt
  disable 8
  continue
end

hbreak AP_InertialSensor::wait_for_sample
commands 9
  silent
  printf "\n[GOOD] wait_for_sample reached\n"
  smallbt
  disable 9
  continue
end

hbreak AP_Scheduler::run
commands 10
  silent
  printf "\n[SUCCESS] AP_Scheduler::run reached\n"
  smallbt
  disable 10
  continue
end

continue