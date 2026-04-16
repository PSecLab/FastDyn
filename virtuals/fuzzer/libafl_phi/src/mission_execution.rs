use baby_fuzzer::gz_state_parser::{
    get_raw_gz_data,
    get_raw_hf_status,
    extract_block_from_gz_data,
    get_sim_time,
    apply_noise
};

use crate::cpexp_input::TargetInput;
use crate::CVG;

use libafl::executors::ExitKind;
use libafl::inputs::{HasTargetBytes, Input};
use std::process::{Command, Stdio, Child};
use std::fs::File;
use std::io::Write;
use gz_transport::Node;
use gz_msgs::clock::Clock;

const RUN_SERVICES_DIR: &str = "../../physics/flight_controllers/courbet/gazebo";
const MAV_C2_DIR: &str = "../../physics/flight_controllers/courbet/mavlink";
const FASTDYN_DIR: &str = "../../..";

/**
 * Assigning each value in file to corresponding index in CVG array.
 * File is of the format: "value,value,..." where each value is u8.
 */
fn deserialize_coverage(path: &str) {
    let contents = std::fs::read_to_string(path).expect("Failed to read coverage file");
    let values: Vec<&str> = contents.trim().split(',').collect();
    unsafe {
        for (i, val_str) in values.iter().enumerate() {
            if i >= CVG.len() {
                break;
            } else {
                let val: u8 = val_str.parse().unwrap_or(0);
                if CVG[i] + val > 255 {
                    CVG[i] = 255;
                } else {
                    CVG[i] += val;
                }
            }
        }
    }
}

/*
    Steps:
    1. Apply inputs
    2. Start Courbet services
        - run_and_attach_services.sh
        - mav_command_and_control.py
        - fd_rover.sh
    3. Monitor for end states and return an ExitKind accordingly:
        - Mission complete (Mavlink C2 exits without error) = ExitKind::Ok
        - Crash (/get_hf_status says "true") = ExitKind::Crash
        - Timeout (sim time from /clock) = ExitKind::Timeout
*/

pub fn execute_mission(
    input: &TargetInput,
    param_names: String,
    cps_name: &str,
    mission_file_name: &str,
    timeout: f64,
    param_input_delay: f64,
    noise_time: f64,
    headless: bool
) -> ExitKind {

    // 1. Apply inputs
    // println!("execute_mission received inputs: {:?}", input);
    let raw_paramater_values = input.get_param_bytes().target_bytes().clone();
    let raw_env_config = input.get_env_config();

    // Writing raw parameter values to a binary file for mav_c2 to read
    let mut bin_param_file: File = File::create("/tmp/mutations.bin").unwrap();
    let _ = bin_param_file.write_all(&raw_paramater_values).unwrap();
    let _ = bin_param_file.sync_all().unwrap();
    drop(bin_param_file);

    // 2. Start Courbet services
    let mut service_binding = Command::new("./run_and_attach_services.sh");
    let mut services_command = service_binding
        .current_dir(RUN_SERVICES_DIR)
        .arg(cps_name);
    if headless {
        services_command.arg("headless");
    }

    let spawn_services = services_command
        .stdout(Stdio::null())
        .spawn();
    if spawn_services.is_err() {
        panic!("Error: Failed to start services: {}", spawn_services.err().unwrap());
    }

    // Let services spin up
    std::thread::sleep(std::time::Duration::from_secs(5));

    let init_file_name: &str;
    if cps_name == "rover" {
        init_file_name = "rover_init.param";
    } else if cps_name == "plane" {
        init_file_name = "plane_init.parm";
    } else {
        panic!("Error: Invalid CPS name: {}", cps_name);
    }

    let mut py_binding = Command::new("python3");
    let mut c2_command = py_binding
        .current_dir(MAV_C2_DIR)
        .arg("ardu_mav_c2.py")
        .arg(init_file_name)
        .arg(mission_file_name)
        .arg(param_names)
        .arg(param_input_delay.to_string())
        .arg("/tmp/mutations.bin");
    if headless {
        c2_command.arg("headless");
    }

    let mut spawn_c2 = c2_command.spawn();
    if spawn_c2.is_err() {
        panic!("Error: Failed to start ardu_mav_c2.py: {}", spawn_c2.err().unwrap());
    }

    // Let mavlink C2 spin up
    std::thread::sleep(std::time::Duration::from_secs(5));

    let script_name: String = format!("configs/{}462.toml", cps_name);
    let spawn_fd = Command::new("fastdyn")
        .current_dir(FASTDYN_DIR)
        .arg("run")
        .arg("-c")
        .arg(script_name)
        .stdout(Stdio::null())
        .spawn();
    if spawn_fd.is_err() {
        panic!("Error: Failed to start FastDyn QEMU: {}", spawn_fd.err().unwrap());
    }

    // 3. Monitor for end states in order of priority
    let mut node: Node = Node::new().unwrap();
    let mut exit_kind: ExitKind;
    let mut noise_applied: bool = false;
    loop {

        // Check for hard fault
        let hf_status: String = get_raw_hf_status(&mut node);
        if hf_status.is_empty() {
            println!("Warning: Failed to get hf_status from Gazebo!");
            exit_kind = ExitKind::Ok;
            break;
        } else if hf_status.find("true").is_some() {
            exit_kind = ExitKind::Crash;
            break;
        }

        // Check for timeout
        let gz_data = get_raw_gz_data(&mut node);
        if gz_data.is_empty() {
            println!("Warning: Failed to get gz_data from Gazebo!");
            exit_kind = ExitKind::Ok;
            break;
        } else {
            let clock_block = extract_block_from_gz_data(&gz_data, "clock");
            let sim_time: f64 = get_sim_time(&clock_block);
            if sim_time >= timeout {
                exit_kind = ExitKind::Timeout;
                break;
            } else if !noise_applied && sim_time >= noise_time {
                apply_noise(raw_env_config.clone(), &mut node, sim_time);
                noise_applied = true;
            }
        }

        // Check for mission complete
        let status = spawn_c2.as_mut().unwrap().try_wait().unwrap();
        if status.is_some() {
            // Mavlink C2 has exited. If the exit code is 0, mission complete.
            if status.unwrap().success() {
                exit_kind = ExitKind::Ok;
                break;
            } else {
                // Timeout
                exit_kind = ExitKind::Timeout;
                break;
            }
        }

    }

    // Just use kill script to force everything dead
    let mut kill = Command::new("./kill.sh")
        .spawn()
        .expect("Failed to spawn kill.sh");
    let _ = kill.wait();

    // Clean up zombies
    let _ = spawn_services.unwrap().wait();
    let _ = spawn_c2.unwrap().wait();
    let _ = spawn_fd.unwrap().wait();

    let cvg_path = format!("{}/fastdyn_work/cvg.bin", FASTDYN_DIR);
    deserialize_coverage(&cvg_path);

    println!("Mission execution finished with exit kind: {:?}", exit_kind);
    exit_kind
}