set pagination off
set confirm off
set print thread-events off
set style enabled off
set breakpoint pending on

# ArduRover 4.6.2 runtime-health probe.
#
# Use this after the sparse/startup scripts prove AP_Scheduler::loop is reached.
# It samples first hits of key scheduler tasks and stops after a bounded number
# of scheduler loops so the firmware does not run forever under GDB.

file virtuals/physics/flight_controllers/courbet/bin/ardurover_v462

directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot
directory /scratch/Fastdyn/ardurover_rehosting_fastdyn/ardupilot/build/CubeBlack

target remote localhost:1234

# Tune this from the GDB prompt before continuing if needed:
#   set $stop_after_scheduler_loops = 500
set $stop_after_scheduler_loops = 200

set $scheduler_loop_hits = 0
set $scheduler_run_seen = 0
set $read_radio_seen = 0
set $ahrs_update_seen = 0
set $mode_update_seen = 0
set $set_servos_seen = 0
set $gps_update_seen = 0
set $baro_update_seen = 0
set $gcs_rx_seen = 0
set $gcs_tx_seen = 0
set $rc_mode_seen = 0
set $ekf_check_seen = 0
set $one_second_seen = 0
set $ins_periodic_seen = 0
set $motors_output_seen = 0
set $motors_regular_seen = 0
set $motors_skid_seen = 0
set $motors_throttle_seen = 0
set $srv_output_all_seen = 0
set $srv_scaled_seen = 0
set $srv_norm_seen = 0
set $srv_pwm_func_seen = 0
set $srv_pwm_seen = 0
set $srv_calc_pwm_seen = 0
set $srv_cork_seen = 0
set $srv_push_seen = 0

printf "\n[runtime] loaded ArduRover runtime-health probe\n"
printf "[runtime] stop_after_scheduler_loops=%d\n", $stop_after_scheduler_loops
printf "[runtime] first-hit task breakpoints plus bounded AP_Scheduler::loop counter\n"

# AP_Scheduler::loop()
hbreak *0x0807c278
commands
  silent
  set $scheduler_loop_hits = $scheduler_loop_hits + 1
  if $scheduler_loop_hits == 1 || $scheduler_loop_hits == 10 || $scheduler_loop_hits == 50 || (($scheduler_loop_hits % 100) == 0)
    printf "\n[LOOP] AP_Scheduler::loop hit=%d pc=%p lr=%p\n", $scheduler_loop_hits, $pc, $lr
  end
  if $scheduler_loop_hits >= $stop_after_scheduler_loops
    printf "\n[STOP] reached scheduler loop budget\n"
    printf "[SUMMARY] loops=%d run=%d read_radio=%d ahrs=%d mode=%d set_servos=%d\n", $scheduler_loop_hits, $scheduler_run_seen, $read_radio_seen, $ahrs_update_seen, $mode_update_seen, $set_servos_seen
    printf "[SUMMARY] gps=%d baro=%d gcs_rx=%d gcs_tx=%d rc_mode=%d ekf=%d one_sec=%d ins_periodic=%d\n", $gps_update_seen, $baro_update_seen, $gcs_rx_seen, $gcs_tx_seen, $rc_mode_seen, $ekf_check_seen, $one_second_seen, $ins_periodic_seen
    printf "[SUMMARY] motors_output=%d regular=%d skid=%d throttle=%d\n", $motors_output_seen, $motors_regular_seen, $motors_skid_seen, $motors_throttle_seen
    printf "[SUMMARY] srv_output_all=%d srv_scaled=%d srv_norm=%d srv_pwm_func=%d srv_pwm_chan=%d srv_calc_pwm=%d srv_cork=%d srv_push=%d\n", $srv_output_all_seen, $srv_scaled_seen, $srv_norm_seen, $srv_pwm_func_seen, $srv_pwm_seen, $srv_calc_pwm_seen, $srv_cork_seen, $srv_push_seen
    printf "[NEXT] If mode/set_servos/motors/SRV stay 0, inspect arming/mode/RC/failsafe.\n"
    printf "[NEXT] If GPS/baro/EKF stay 0 or unhealthy, inspect physics sensor feeds.\n"
    bt 8
  else
    continue
  end
end

# AP_Scheduler::run(unsigned long)
thbreak *0x0807bd14
commands
  silent
  set $scheduler_run_seen = 1
  printf "\n[TASK] AP_Scheduler::run first hit time_available=%u pc=%p\n", $r1, $pc
  continue
end

# Rover::read_radio()
thbreak *0x08019eb4
commands
  silent
  set $read_radio_seen = 1
  printf "\n[TASK] Rover::read_radio first hit pc=%p\n", $pc
  continue
end

# Rover::ahrs_update()
thbreak *0x0801410c
commands
  silent
  set $ahrs_update_seen = 1
  printf "\n[TASK] Rover::ahrs_update first hit pc=%p\n", $pc
  continue
end

# Rover::update_current_mode()
thbreak *0x08014480
commands
  silent
  set $mode_update_seen = 1
  printf "\n[TASK] Rover::update_current_mode first hit pc=%p\n", $pc
  continue
end

# Rover::set_servos()
thbreak *0x080147ec
commands
  silent
  set $set_servos_seen = 1
  printf "\n[TASK] Rover::set_servos first hit pc=%p\n", $pc
  continue
end

# AP_GPS::update()
thbreak *0x080341bc
commands
  silent
  set $gps_update_seen = 1
  printf "\n[TASK] AP_GPS::update first hit pc=%p\n", $pc
  continue
end

# AP_Baro::update()
thbreak *0x08021138
commands
  silent
  set $baro_update_seen = 1
  printf "\n[TASK] AP_Baro::update first hit pc=%p\n", $pc
  continue
end

# GCS::update_receive()
thbreak *0x0808c4dc
commands
  silent
  set $gcs_rx_seen = 1
  printf "\n[TASK] GCS::update_receive first hit pc=%p\n", $pc
  continue
end

# GCS::update_send()
thbreak *0x0808d998
commands
  silent
  set $gcs_tx_seen = 1
  printf "\n[TASK] GCS::update_send first hit pc=%p\n", $pc
  continue
end

# RC_Channels::read_mode_switch()
thbreak *0x0808ec78
commands
  silent
  set $rc_mode_seen = 1
  printf "\n[TASK] RC_Channels::read_mode_switch first hit pc=%p\n", $pc
  continue
end

# Rover::ekf_check()
thbreak *0x08015028
commands
  silent
  set $ekf_check_seen = 1
  printf "\n[TASK] Rover::ekf_check first hit pc=%p\n", $pc
  continue
end

# Rover::one_second_loop()
thbreak *0x08014398
commands
  silent
  set $one_second_seen = 1
  printf "\n[TASK] Rover::one_second_loop first hit pc=%p\n", $pc
  continue
end

# AP_InertialSensor::periodic()
thbreak *0x08041700
commands
  silent
  set $ins_periodic_seen = 1
  printf "\n[TASK] AP_InertialSensor::periodic first hit pc=%p\n", $pc
  continue
end

# AP_MotorsUGV::output(bool, float, float)
thbreak *0x080e4618
commands
  silent
  set $motors_output_seen = 1
  printf "\n[OUTPUT] AP_MotorsUGV::output first hit pc=%p r0=%p r1=0x%x\n", $pc, $r0, $r1
  continue
end

# AP_MotorsUGV::output_regular(bool, float, float, float)
thbreak *0x080e3ec0
commands
  silent
  set $motors_regular_seen = 1
  printf "\n[OUTPUT] AP_MotorsUGV::output_regular first hit pc=%p r0=%p\n", $pc, $r0
  continue
end

# AP_MotorsUGV::output_skid_steering(bool, float, float, float)
thbreak *0x080e40d4
commands
  silent
  set $motors_skid_seen = 1
  printf "\n[OUTPUT] AP_MotorsUGV::output_skid_steering first hit pc=%p r0=%p\n", $pc, $r0
  continue
end

# AP_MotorsUGV::output_throttle(SRV_Channel::Aux_servo_function_t, float, float)
thbreak *0x080e3c3c
commands
  silent
  set $motors_throttle_seen = 1
  printf "\n[OUTPUT] AP_MotorsUGV::output_throttle first hit function=%u pc=%p\n", ($r1 & 0xffff), $pc
  continue
end

# SRV_Channels::output_ch_all()
thbreak *0x08090650
commands
  silent
  set $srv_output_all_seen = 1
  printf "\n[OUTPUT] SRV_Channels::output_ch_all first hit pc=%p\n", $pc
  continue
end

# SRV_Channels::set_output_scaled(SRV_Channel::Aux_servo_function_t, float)
thbreak *0x08090874
commands
  silent
  set $srv_scaled_seen = 1
  printf "\n[OUTPUT] SRV_Channels::set_output_scaled first hit function=%u pc=%p\n", ($r0 & 0xffff), $pc
  continue
end

# SRV_Channels::set_output_norm(SRV_Channel::Aux_servo_function_t, float)
thbreak *0x08091030
commands
  silent
  set $srv_norm_seen = 1
  printf "\n[OUTPUT] SRV_Channels::set_output_norm first hit function=%u pc=%p\n", ($r0 & 0xffff), $pc
  continue
end

# SRV_Channels::set_output_pwm(SRV_Channel::Aux_servo_function_t, uint16_t)
thbreak *0x08090d84
commands
  silent
  set $srv_pwm_func_seen = 1
  printf "\n[OUTPUT] SRV_Channels::set_output_pwm first hit function=%u pwm=%u pc=%p\n", ($r0 & 0xffff), ($r1 & 0xffff), $pc
  continue
end

# SRV_Channels::set_output_pwm_chan(uint8_t, uint16_t)
thbreak *0x08091260
commands
  silent
  set $srv_pwm_seen = 1
  printf "\n[OUTPUT] SRV_Channels::set_output_pwm_chan first hit chan=%u pwm=%u pc=%p\n", ($r0 & 0xff), ($r1 & 0xffff), $pc
  continue
end

# SRV_Channels::calc_pwm()
thbreak *0x0809116c
commands
  silent
  set $srv_calc_pwm_seen = 1
  printf "\n[OUTPUT] SRV_Channels::calc_pwm first hit pc=%p\n", $pc
  continue
end

# SRV_Channels::cork()
thbreak *0x08091348
commands
  silent
  set $srv_cork_seen = 1
  printf "\n[OUTPUT] SRV_Channels::cork first hit pc=%p\n", $pc
  continue
end

# SRV_Channels::push()
thbreak *0x0809135c
commands
  silent
  set $srv_push_seen = 1
  printf "\n[OUTPUT] SRV_Channels::push first hit pc=%p\n", $pc
  continue
end

printf "[runtime] breakpoints installed; continuing target\n"
continue
