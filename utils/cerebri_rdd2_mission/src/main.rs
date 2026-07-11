mod physics;
mod protocol;
mod shared_memory;

use std::env;
use std::fs;
use std::path::PathBuf;
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, anyhow, bail};
use physics::{Quadrotor, radians_to_degrees};
use protocol::{FlightState, MotorCommand};
use serde::Serialize;
use synapse_fbs::topic_catalog;
use zenoh::{Wait, config::Config};

type Subscriber =
    zenoh::pubsub::Subscriber<zenoh::handlers::FifoChannelHandler<zenoh::sample::Sample>>;

const DEFAULT_PLANT_DT: f64 = 0.005;
/// RDD2's fixed 1600 Hz firmware control-loop period.
const CONTROLLER_DT: f64 = 0.000_625;

#[derive(Debug)]
struct Options {
    locator: String,
    report: PathBuf,
    duration: f64,
    response_timeout: Duration,
    controller_benchmark: Option<f64>,
    shared_memory: Option<PathBuf>,
    firmware_elf: Option<PathBuf>,
    plant_dt: f64,
    minimum_speedup: f64,
}

#[derive(Debug, Default, Serialize)]
struct Report {
    passed: bool,
    simulated_seconds: f64,
    wall_seconds: f64,
    speedup_over_realtime: f64,
    minimum_speedup_required: f64,
    plant_step_seconds: f64,
    controller_ticks_expected: u64,
    plant_steps: u64,
    motor_messages: u64,
    flight_state_messages: u64,
    max_altitude_m: f64,
    max_tilt_deg: f64,
    max_roll_response_deg: f64,
    max_pitch_response_deg: f64,
    final_altitude_m: f64,
    final_vertical_speed_m_s: f64,
    firmware_armed_observed: bool,
    firmware_disarmed_after_flight: bool,
    firmware_rc_and_imu_healthy: bool,
    firmware_auto_level_observed: bool,
    failures: Vec<String>,
}

fn options() -> Result<Options> {
    let mut locator = "udp/192.0.2.2:7447".to_owned();
    let mut report = PathBuf::from("out/cerebri_rdd2_mission.json");
    let mut duration: f64 = 20.0;
    let mut timeout_ms = 2_000_u64;
    let mut controller_benchmark = env::var("RDD2_FASTDYN_CONTROLLER_BENCHMARK_S")
        .ok()
        .map(|value| value.parse())
        .transpose()?;
    let mut shared_memory = env::var_os("RDD2_FASTDYN_SHARED_MEMORY").map(PathBuf::from);
    let mut firmware_elf = env::var_os("RDD2_FASTDYN_FIRMWARE_ELF").map(PathBuf::from);
    let mut plant_dt = env::var("RDD2_FASTDYN_PLANT_DT_S")
        .ok()
        .map(|value| value.parse())
        .transpose()?
        .unwrap_or(DEFAULT_PLANT_DT);
    let mut minimum_speedup: f64 = env::var("RDD2_FASTDYN_MIN_SPEEDUP")
        .ok()
        .map(|value| value.parse())
        .transpose()?
        .unwrap_or(0.0);
    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        let value = || anyhow!("{arg} requires a value");
        match arg.as_str() {
            "--locator" => locator = args.next().ok_or_else(value)?,
            "--report" => report = args.next().ok_or_else(value)?.into(),
            "--duration" => duration = args.next().ok_or_else(value)?.parse()?,
            "--response-timeout-ms" => timeout_ms = args.next().ok_or_else(value)?.parse()?,
            "--controller-benchmark" => {
                controller_benchmark = Some(args.next().ok_or_else(value)?.parse()?)
            }
            "--shared-memory" => shared_memory = Some(args.next().ok_or_else(value)?.into()),
            "--firmware-elf" => firmware_elf = Some(args.next().ok_or_else(value)?.into()),
            "--plant-dt" => plant_dt = args.next().ok_or_else(value)?.parse()?,
            "--minimum-speedup" => minimum_speedup = args.next().ok_or_else(value)?.parse()?,
            "-h" | "--help" => {
                println!(
                    "cerebri-rdd2-mission [--locator LOCATOR] [--report PATH] [--duration SEC] [--controller-benchmark SEC] [--shared-memory PATH --firmware-elf PATH] [--plant-dt SEC] [--minimum-speedup X]"
                );
                std::process::exit(0);
            }
            _ => bail!("unknown argument: {arg}"),
        }
    }
    if !duration.is_finite() || duration < 18.0 {
        bail!("--duration must be finite and at least 18 seconds");
    }
    if !plant_dt.is_finite() || !(CONTROLLER_DT..=0.020).contains(&plant_dt) {
        bail!("--plant-dt must be finite and between 0.000625 and 0.020 seconds");
    }
    if !minimum_speedup.is_finite() || minimum_speedup < 0.0 {
        bail!("--minimum-speedup must be finite and non-negative");
    }
    Ok(Options {
        locator,
        report,
        duration,
        response_timeout: Duration::from_millis(timeout_ms),
        controller_benchmark,
        shared_memory,
        firmware_elf,
        plant_dt,
        minimum_speedup,
    })
}

fn publish_inputs(
    manual_publisher: &zenoh::pubsub::Publisher<'_>,
    inertial_publisher: &zenoh::pubsub::Publisher<'_>,
    manual_topic: &str,
    inertial_topic: &str,
    inputs: &protocol::LockstepInputs,
) -> Result<()> {
    manual_publisher
        .put(inputs.manual_control.0)
        .wait()
        .map_err(|error| anyhow!("cannot publish {manual_topic}: {error}"))?;
    inertial_publisher
        .put(inputs.inertial_sample.0)
        .wait()
        .map_err(|error| anyhow!("cannot publish {inertial_topic}: {error}"))?;
    Ok(())
}

fn zenoh_config(locator: &str) -> Result<Config> {
    let mut config = Config::default();
    config
        .insert_json5("mode", "\"client\"")
        .map_err(|error| anyhow!("failed to configure Zenoh mode: {error}"))?;
    config
        .insert_json5("connect/endpoints", &format!("[\"{locator}\"]"))
        .map_err(|error| anyhow!("failed to configure Zenoh endpoint: {error}"))?;
    config
        .insert_json5("transport/link/tx/queue/batching/enabled", "false")
        .map_err(|error| anyhow!("failed to disable Zenoh batching: {error}"))?;
    Ok(config)
}

fn drain(subscriber: &Subscriber) -> Result<()> {
    while subscriber
        .try_recv()
        .map_err(|error| anyhow!("Zenoh receive failed: {error}"))?
        .is_some()
    {}
    Ok(())
}

fn receive_motor(subscriber: &Subscriber, timeout: Duration, topic: &str) -> Result<MotorCommand> {
    let deadline = Instant::now() + timeout;
    let mut idle = 0_u32;
    loop {
        if let Some(sample) = subscriber
            .try_recv()
            .map_err(|error| anyhow!("failed to receive {topic}: {error}"))?
        {
            let bytes = sample.payload().to_bytes();
            if let Ok(command) = protocol::motor_output(bytes.as_ref()) {
                return Ok(command);
            }
        }
        if Instant::now() >= deadline {
            bail!("timed out waiting for {topic}");
        }
        idle = idle.wrapping_add(1);
        if idle.is_multiple_of(256) {
            thread::yield_now();
        } else {
            std::hint::spin_loop();
        }
    }
}

fn latest_flight_state(subscriber: &Subscriber, topic: &str) -> Result<(Option<FlightState>, u64)> {
    let mut latest = None;
    let mut count = 0;
    while let Some(sample) = subscriber
        .try_recv()
        .map_err(|error| anyhow!("failed to receive {topic}: {error}"))?
    {
        let bytes = sample.payload().to_bytes();
        if let Ok(state) = protocol::flight_state(bytes.as_ref()) {
            latest = Some(state);
            count += 1;
        }
    }
    Ok((latest, count))
}

fn desired_altitude(time: f64) -> f64 {
    match time {
        t if t < 2.0 => 0.0,
        t if t < 5.0 => (t - 2.0) * (2.0 / 3.0),
        t if t < 14.0 => 2.0,
        t if t < 18.0 => 2.0 - (t - 14.0) * 0.5,
        _ => 0.0,
    }
}

fn rc_channels(time: f64, plant: &Quadrotor) -> [i32; 16] {
    let mut channels = [1500; 16];
    let armed = (1.0..19.0).contains(&time);
    channels[4] = if armed { 2000 } else { 1000 };
    channels[5] = 2000; // AUTO_LEVEL exercises the attitude estimator/controller.
    if !(1.25..19.0).contains(&time) {
        channels[2] = 1000;
    } else {
        let error = desired_altitude(time) - plant.altitude();
        let normalized = (0.688 + 0.10 * error - 0.075 * plant.vertical_speed()).clamp(0.38, 0.82);
        channels[2] = (1000.0 + normalized * 1000.0).round() as i32;
    }
    if (8.0..9.0).contains(&time) {
        channels[0] = 1625;
    }
    if (11.0..12.0).contains(&time) {
        channels[1] = 1375;
    }
    channels
}

fn evaluate(report: &mut Report) {
    if report.speedup_over_realtime < report.minimum_speedup_required {
        report.failures.push(format!(
            "simulation speed {:.3}x is below required {:.3}x",
            report.speedup_over_realtime, report.minimum_speedup_required
        ));
    }
    if !report.firmware_armed_observed {
        report.failures.push("firmware never armed".into());
    }
    if !report.firmware_disarmed_after_flight {
        report
            .failures
            .push("firmware did not disarm after landing".into());
    }
    if !report.firmware_rc_and_imu_healthy {
        report
            .failures
            .push("firmware did not report valid RC and IMU state".into());
    }
    if !report.firmware_auto_level_observed {
        report
            .failures
            .push("firmware did not enter AUTO_LEVEL".into());
    }
    if report.flight_state_messages < 20 {
        report
            .failures
            .push("too few VehicleHealth messages".into());
    }
    if report.max_altitude_m < 1.0 {
        report
            .failures
            .push("vehicle did not take off above 1 m".into());
    }
    if report.max_roll_response_deg < 2.0 {
        report
            .failures
            .push("roll maneuver produced less than 2 degrees".into());
    }
    if report.max_pitch_response_deg < 2.0 {
        report
            .failures
            .push("pitch maneuver produced less than 2 degrees".into());
    }
    if report.max_tilt_deg > 45.0 {
        report
            .failures
            .push("vehicle exceeded 45 degrees tilt".into());
    }
    if report.final_altitude_m > 0.20 {
        report
            .failures
            .push("vehicle did not land within 0.20 m".into());
    }
    if report.final_vertical_speed_m_s.abs() > 0.50 {
        report
            .failures
            .push("vehicle had excessive vertical speed at completion".into());
    }
    report.passed = report.failures.is_empty();
}

fn write_report(path: &PathBuf, report: &Report) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(path, serde_json::to_vec_pretty(report)?)
        .with_context(|| format!("cannot write report {}", path.display()))
}

fn run_controller_benchmark<F>(options: &Options, exchange: &mut F) -> Result<()>
where
    F: FnMut(
        &protocol::LockstepInputs,
        Duration,
    ) -> Result<(MotorCommand, Option<FlightState>, u64)>,
{
    let benchmark_seconds = options
        .controller_benchmark
        .context("controller benchmark duration is missing")?;
    if !benchmark_seconds.is_finite() || benchmark_seconds <= 0.0 {
        bail!("--controller-benchmark must be a positive finite duration");
    }
    let plant = Quadrotor::default();
    let (gyro, accel) = plant.imu_flu();
    let mut channels = [1500; 16];
    channels[2] = 1000;
    channels[4] = 1000;
    channels[5] = 2000;
    let warmup_target_ns = 5_000_000_u64;
    let ready_deadline = Instant::now() + Duration::from_secs(60);
    loop {
        let inputs = protocol::lockstep_inputs(gyro, accel, channels, warmup_target_ns);
        if exchange(&inputs, options.response_timeout).is_ok() {
            break;
        }
        if Instant::now() >= ready_deadline {
            bail!("firmware did not become ready for controller benchmark");
        }
    }

    let target_ns = warmup_target_ns + (benchmark_seconds * 1.0e9).round() as u64;
    let inputs = protocol::lockstep_inputs(gyro, accel, channels, target_ns);
    let start = Instant::now();
    exchange(&inputs, Duration::from_secs(120))?;
    let wall_seconds = start.elapsed().as_secs_f64();
    println!(
        "RDD2_CONTROLLER_BENCHMARK simulated_s={benchmark_seconds:.6} wall_s={wall_seconds:.6} speedup={:.3}x",
        benchmark_seconds / wall_seconds
    );
    Ok(())
}

fn run_mission<F>(options: &Options, exchange: &mut F) -> Result<()>
where
    F: FnMut(
        &protocol::LockstepInputs,
        Duration,
    ) -> Result<(MotorCommand, Option<FlightState>, u64)>,
{
    let mut plant = Quadrotor::default();
    let mut report = Report::default();
    let mut simulated_time = 0.0_f64;
    let wall_start = Instant::now();
    let mut firmware_ready = false;
    let mut firmware_armed = false;

    let mission_steps = (options.duration / options.plant_dt).round() as u64;
    while report.plant_steps < mission_steps {
        let channels = rc_channels(simulated_time, &plant);
        let (gyro, accel) = plant.imu_flu();
        let target_time = ((simulated_time + options.plant_dt) * 1.0e9).round() as u64;
        let inputs = protocol::lockstep_inputs(gyro, accel, channels, target_time);
        let (motor, state, state_count) = match exchange(&inputs, options.response_timeout) {
            Ok(response) => response,
            Err(error) if !firmware_ready && wall_start.elapsed() < Duration::from_secs(60) => {
                eprintln!("waiting for rehosted RDD2 firmware: {error}");
                continue;
            }
            Err(error) => return Err(error),
        };
        report.flight_state_messages += state_count;
        if !firmware_ready {
            if state.is_none() && wall_start.elapsed() < Duration::from_secs(60) {
                eprintln!("waiting for first post-input VehicleHealth response");
                continue;
            }
            if state.is_none() {
                bail!("firmware produced PWM but no post-input VehicleHealth response");
            }
            firmware_ready = true;
        }

        plant.step(motor.values, options.plant_dt);
        simulated_time += options.plant_dt;
        report.plant_steps += 1;
        report.motor_messages += 1;

        if let Some(state) = state {
            firmware_armed = state.armed;
            report.firmware_rc_and_imu_healthy |= state.rc_valid && state.imu_ok;
            report.firmware_auto_level_observed |= state.flight_mode == 1;
            report.firmware_armed_observed |= state.armed;
            if simulated_time >= 19.25 && !state.armed {
                report.firmware_disarmed_after_flight = true;
            }
        }

        let euler = plant.euler();
        let roll = radians_to_degrees(euler[0]).abs();
        let pitch = radians_to_degrees(euler[1]).abs();
        report.max_altitude_m = report.max_altitude_m.max(plant.altitude());
        report.max_tilt_deg = report.max_tilt_deg.max(roll.max(pitch));
        if (8.0..10.5).contains(&simulated_time) {
            report.max_roll_response_deg = report.max_roll_response_deg.max(roll);
        }
        if (11.0..13.5).contains(&simulated_time) {
            report.max_pitch_response_deg = report.max_pitch_response_deg.max(pitch);
        }
        let progress_steps = (1.0 / options.plant_dt).round() as u64;
        if report.plant_steps.is_multiple_of(progress_steps) {
            println!(
                "[rdd2-mission] t={simulated_time:.1}s alt={:.2}m vz={:.2}m/s tilt={:.1}deg armed={}",
                plant.altitude(),
                plant.vertical_speed(),
                roll.max(pitch),
                firmware_armed,
            );
        }
    }

    report.simulated_seconds = report.plant_steps as f64 * options.plant_dt;
    report.wall_seconds = wall_start.elapsed().as_secs_f64();
    report.speedup_over_realtime = report.simulated_seconds / report.wall_seconds;
    report.minimum_speedup_required = options.minimum_speedup;
    report.plant_step_seconds = options.plant_dt;
    // Derived from the simulated duration at the 1600 Hz control rate, not
    // counted from firmware telemetry.
    report.controller_ticks_expected = (report.simulated_seconds / CONTROLLER_DT).round() as u64;
    report.final_altitude_m = plant.altitude();
    report.final_vertical_speed_m_s = plant.vertical_speed();
    evaluate(&mut report);
    write_report(&options.report, &report)?;
    println!(
        "RDD2_MISSION_RESULT passed={} simulated_s={:.3} wall_s={:.3} speedup={:.2}x max_alt_m={:.2} report={}",
        report.passed,
        report.simulated_seconds,
        report.wall_seconds,
        report.speedup_over_realtime,
        report.max_altitude_m,
        options.report.display(),
    );
    if !report.passed {
        bail!("RDD2 mission failed: {}", report.failures.join("; "));
    }
    Ok(())
}

fn run_shared_memory(options: &Options, memory_path: &PathBuf) -> Result<()> {
    let firmware_elf = options.firmware_elf.as_ref().ok_or_else(|| {
        anyhow!("--firmware-elf or RDD2_FASTDYN_FIRMWARE_ELF is required with shared memory")
    })?;
    println!(
        "RDD2 mission runner connected to FastDyn shared memory {}",
        memory_path.display()
    );
    let mut transport =
        shared_memory::Transport::open(memory_path, firmware_elf, Duration::from_secs(60))?;
    let mut exchange = |inputs: &protocol::LockstepInputs, timeout: Duration| {
        let (motor, state) = transport.exchange(inputs, timeout)?;
        Ok((motor, Some(state), 1))
    };
    if options.controller_benchmark.is_some() {
        run_controller_benchmark(options, &mut exchange)
    } else {
        run_mission(options, &mut exchange)
    }
}

fn run_zenoh(options: &Options) -> Result<()> {
    let topic_key = |name| {
        topic_catalog::topic_by_name(name)
            .map(|info| info.key)
            .ok_or_else(|| anyhow!("synapse_fbs v{} has no {name} topic", synapse_fbs::VERSION))
    };
    let manual_topic = topic_key("ManualControlCommand")?;
    let inertial_topic = topic_key("InertialSample")?;
    let motor_topic = topic_key("PwmSignalOutputs")?;
    let health_topic = topic_key("VehicleHealth")?;
    let session = zenoh::open(zenoh_config(&options.locator)?)
        .wait()
        .map_err(|error| {
            anyhow!(
                "cannot connect to Zenoh router {}: {error}",
                options.locator
            )
        })?;
    let manual_publisher = session
        .declare_publisher(manual_topic)
        .wait()
        .map_err(|error| anyhow!("cannot declare {manual_topic}: {error}"))?;
    let inertial_publisher = session
        .declare_publisher(inertial_topic)
        .wait()
        .map_err(|error| anyhow!("cannot declare {inertial_topic}: {error}"))?;
    let motor_subscriber = session
        .declare_subscriber(motor_topic)
        .wait()
        .map_err(|error| anyhow!("cannot subscribe to {motor_topic}: {error}"))?;
    let flight_subscriber = session
        .declare_subscriber(health_topic)
        .wait()
        .map_err(|error| anyhow!("cannot subscribe to {health_topic}: {error}"))?;
    println!("RDD2 mission runner connected to {}", options.locator);

    let mut exchange = |inputs: &protocol::LockstepInputs, timeout: Duration| {
        drain(&motor_subscriber)?;
        publish_inputs(
            &manual_publisher,
            &inertial_publisher,
            manual_topic,
            inertial_topic,
            inputs,
        )?;

        let motor = receive_motor(&motor_subscriber, timeout, motor_topic)?;
        let (state, count) = latest_flight_state(&flight_subscriber, health_topic)?;
        Ok((motor, state, count))
    };
    if options.controller_benchmark.is_some() {
        run_controller_benchmark(options, &mut exchange)
    } else {
        run_mission(options, &mut exchange)
    }
}

fn main() -> Result<()> {
    let options = options()?;
    if let Some(memory_path) = &options.shared_memory {
        run_shared_memory(&options, memory_path)
    } else {
        run_zenoh(&options)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn nominal_report() -> Report {
        Report {
            speedup_over_realtime: 12.0,
            minimum_speedup_required: 10.0,
            firmware_armed_observed: true,
            firmware_disarmed_after_flight: true,
            firmware_rc_and_imu_healthy: true,
            firmware_auto_level_observed: true,
            flight_state_messages: 100,
            max_altitude_m: 2.0,
            max_roll_response_deg: 5.0,
            max_pitch_response_deg: 5.0,
            max_tilt_deg: 20.0,
            final_altitude_m: 0.05,
            final_vertical_speed_m_s: 0.0,
            ..Report::default()
        }
    }

    #[test]
    fn evaluate_passes_a_nominal_mission() {
        let mut report = nominal_report();
        evaluate(&mut report);
        assert!(report.passed, "unexpected failures: {:?}", report.failures);
        assert!(report.failures.is_empty());
    }

    #[test]
    fn evaluate_flags_each_violated_requirement() {
        let mut report = nominal_report();
        report.speedup_over_realtime = 9.0;
        report.firmware_armed_observed = false;
        report.max_tilt_deg = 50.0;
        report.final_altitude_m = 0.5;
        evaluate(&mut report);
        assert!(!report.passed);
        assert_eq!(report.failures.len(), 4, "failures: {:?}", report.failures);
    }
}
