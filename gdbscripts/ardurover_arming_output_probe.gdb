set pagination off
set confirm off
set print thread-events off
set style enabled off
set breakpoint pending on

# ArduRover 4.6.2 arming/output decision probe.
#
# Run after AP_Scheduler::loop is known to work. This script distinguishes:
# - no arm attempt
# - AP_Arming false before motor output
# - soft-armed false inside AP_MotorsUGV::output()
# - active mode commands throttle/steering but output remains safety-limited
# - failsafe events forcing mode/output changes

file virtuals/physics/flight_controllers/courbet/bin/ardurover_v462

directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot
directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/build/CubeBlack

target remote localhost:1234

set $stop_after_scheduler_loops = 300

set $loop_hits = 0
set $motor_output_hits = 0
set $motor_input_armed_seen = 0
set $motor_input_disarmed_seen = 0
set $regular_armed_seen = 0
set $regular_disarmed_seen = 0
set $skid_armed_seen = 0
set $skid_disarmed_seen = 0
set $throttle_output_hits = 0
set $set_throttle_seen = 0
set $set_steering_seen = 0
set $mode_manual_seen = 0
set $mode_hold_seen = 0
set $mode_steering_seen = 0
set $mode_auto_seen = 0
set $mode_guided_seen = 0
set $mode_loiter_seen = 0
set $mode_rtl_seen = 0
set $mode_acro_seen = 0
set $mode_initializing_seen = 0
set $arm_attempt_seen = 0
set $disarm_seen = 0
set $arm_checks_seen = 0
set $prearm_checks_seen = 0
set $soft_update_seen = 0
set $failsafe_trigger_seen = 0

printf "\n[armout] loaded arming/output decision probe\n"
printf "[armout] stop_after_scheduler_loops=%d\n", $stop_after_scheduler_loops

# AP_Scheduler::loop()
hbreak *0x0807c278
commands
  silent
  set $loop_hits = $loop_hits + 1
  if $loop_hits == 1 || $loop_hits == 10 || $loop_hits == 50 || (($loop_hits % 100) == 0)
    printf "\n[LOOP] scheduler loop hit=%d\n", $loop_hits
  end
  if $loop_hits >= $stop_after_scheduler_loops
    printf "\n[STOP] reached loop budget\n"
    printf "[SUMMARY] loops=%d motor_output_hits=%d input_armed=%d input_disarmed=%d\n", $loop_hits, $motor_output_hits, $motor_input_armed_seen, $motor_input_disarmed_seen
    printf "[SUMMARY] regular_armed=%d regular_disarmed=%d skid_armed=%d skid_disarmed=%d output_throttle_hits=%d\n", $regular_armed_seen, $regular_disarmed_seen, $skid_armed_seen, $skid_disarmed_seen, $throttle_output_hits
    printf "[SUMMARY] set_throttle=%d set_steering=%d\n", $set_throttle_seen, $set_steering_seen
    printf "[SUMMARY] modes manual=%d hold=%d steering=%d auto=%d guided=%d loiter=%d rtl=%d acro=%d initializing=%d\n", $mode_manual_seen, $mode_hold_seen, $mode_steering_seen, $mode_auto_seen, $mode_guided_seen, $mode_loiter_seen, $mode_rtl_seen, $mode_acro_seen, $mode_initializing_seen
    printf "[SUMMARY] arm_attempt=%d disarm=%d arm_checks=%d prearm_checks=%d soft_update=%d failsafe_trigger=%d\n", $arm_attempt_seen, $disarm_seen, $arm_checks_seen, $prearm_checks_seen, $soft_update_seen, $failsafe_trigger_seen
    printf "[INTERPRET] input_disarmed=1 with regular_disarmed=1 means AP_Arming is false.\n"
    printf "[INTERPRET] input_armed=1 but regular_disarmed=1 means soft-armed/safety state is clearing output.\n"
    printf "[INTERPRET] mode/set_throttle hit but output_throttle=0 means armed path is not active.\n"
    bt 8
  else
    continue
  end
end

# AP_MotorsUGV::output(bool armed, float ground_speed, float dt)
# r1 is the bool armed argument from Rover::set_servos(), before soft-armed
# override inside AP_MotorsUGV::output().
hbreak *0x080e4618
commands
  silent
  set $motor_output_hits = $motor_output_hits + 1
  if ($r1 & 1)
    set $motor_input_armed_seen = 1
  else
    set $motor_input_disarmed_seen = 1
  end
  if $motor_output_hits <= 5 || (($motor_output_hits % 100) == 0)
    printf "\n[MOTOR] AP_MotorsUGV::output hit=%d input_armed_arg=%u pc=%p\n", $motor_output_hits, ($r1 & 1), $pc
  end
  continue
end

# AP_MotorsUGV::output_regular(bool armed, float ground_speed, float steering, float throttle)
# r1 is the bool armed argument after AP_MotorsUGV::output() has applied the
# soft-armed override.
hbreak *0x080e3ec0
commands
  silent
  if ($r1 & 1)
    set $regular_armed_seen = 1
  else
    set $regular_disarmed_seen = 1
  end
  if ($r1 & 1) || $motor_output_hits <= 5
    printf "\n[MOTOR] output_regular armed_arg=%u pc=%p\n", ($r1 & 1), $pc
  end
  continue
end

# AP_MotorsUGV::output_skid_steering(bool armed, float steering, float throttle, float dt)
hbreak *0x080e40d4
commands
  silent
  if ($r1 & 1)
    set $skid_armed_seen = 1
  else
    set $skid_disarmed_seen = 1
  end
  if ($r1 & 1) || $motor_output_hits <= 5
    printf "\n[MOTOR] output_skid_steering armed_arg=%u pc=%p\n", ($r1 & 1), $pc
  end
  continue
end

# AP_MotorsUGV::output_throttle(SRV_Channel::Aux_servo_function_t, float, float)
hbreak *0x080e3c3c
commands
  silent
  set $throttle_output_hits = $throttle_output_hits + 1
  if $throttle_output_hits <= 8
    printf "\n[THROTTLE] output_throttle hit=%d function=%u pc=%p\n", $throttle_output_hits, ($r1 & 0xffff), $pc
  end
  continue
end

# AP_MotorsUGV::set_throttle(float)
thbreak *0x080e3038
commands
  silent
  set $set_throttle_seen = 1
  printf "\n[CMD] AP_MotorsUGV::set_throttle first hit pc=%p\n", $pc
  continue
end

# AP_MotorsUGV::set_steering(float, bool)
thbreak *0x080e302c
commands
  silent
  set $set_steering_seen = 1
  printf "\n[CMD] AP_MotorsUGV::set_steering first hit pc=%p\n", $pc
  continue
end

# Active mode first-hit probes.
thbreak *0x08019184
commands
  silent
  set $mode_manual_seen = 1
  printf "\n[MODE] ModeManual::update first hit\n"
  continue
end

thbreak *0x08018e20
commands
  silent
  set $mode_hold_seen = 1
  printf "\n[MODE] ModeHold::update first hit\n"
  continue
end

thbreak *0x08019714
commands
  silent
  set $mode_steering_seen = 1
  printf "\n[MODE] ModeSteering::update first hit\n"
  continue
end

thbreak *0x08016e4c
commands
  silent
  set $mode_auto_seen = 1
  printf "\n[MODE] ModeAuto::update first hit\n"
  continue
end

thbreak *0x08018a70
commands
  silent
  set $mode_guided_seen = 1
  printf "\n[MODE] ModeGuided::update first hit\n"
  continue
end

thbreak *0x08018f0c
commands
  silent
  set $mode_loiter_seen = 1
  printf "\n[MODE] ModeLoiter::update first hit\n"
  continue
end

thbreak *0x0801932c
commands
  silent
  set $mode_rtl_seen = 1
  printf "\n[MODE] ModeRTL::update first hit\n"
  continue
end

thbreak *0x080162e8
commands
  silent
  set $mode_acro_seen = 1
  printf "\n[MODE] ModeAcro::update first hit\n"
  continue
end

thbreak *0x080134a8
commands
  silent
  set $mode_initializing_seen = 1
  printf "\n[MODE] ModeInitializing::update first hit\n"
  continue
end

# Arming/failsafe events.
hbreak *0x08010c88
commands
  silent
  set $arm_attempt_seen = 1
  printf "\n[ARM] AP_Arming_Rover::arm called method=%u force=%u pc=%p\n", $r1, $r2, $pc
  bt 8
  continue
end

hbreak *0x08010cdc
commands
  silent
  set $disarm_seen = 1
  printf "\n[ARM] AP_Arming_Rover::disarm called method=%u force=%u pc=%p\n", $r1, $r2, $pc
  bt 8
  continue
end

thbreak *0x08010c78
commands
  silent
  set $arm_checks_seen = 1
  printf "\n[ARM] AP_Arming_Rover::arm_checks first hit method=%u pc=%p\n", $r1, $pc
  continue
end

thbreak *0x08010bfc
commands
  silent
  set $prearm_checks_seen = 1
  printf "\n[ARM] AP_Arming_Rover::pre_arm_checks first hit report=%u pc=%p\n", $r1, $pc
  continue
end

thbreak *0x08010968
commands
  silent
  set $soft_update_seen = 1
  printf "\n[ARM] AP_Arming_Rover::update_soft_armed first hit pc=%p\n", $pc
  continue
end

hbreak *0x0801518c
commands
  silent
  set $failsafe_trigger_seen = 1
  printf "\n[FAILSAFE] Rover::failsafe_trigger type=%u on=%u pc=%p\n", ($r1 & 0xff), ($r3 & 0xff), $pc
  bt 8
  continue
end

printf "[armout] breakpoints installed; continuing target\n"
continue
