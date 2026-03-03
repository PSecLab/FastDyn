use baby_fuzzer::gz_state_parser::{
    get_raw_gz_data,
    get_raw_hf_status,
    extract_block_from_gz_data,
    get_sim_time,
};

use crate::cpexp_input::TargetInput;
use crate::CVG;

use libafl::executors::ExitKind;
use libafl::inputs::HasTargetBytes;
use std::process::{Command, Stdio, Child};
use std::fs::File;
use std::io::Write;
use gz_transport::Node;
use gz_msgs::clock::Clock;

const RUN_SERVICES_DIR: &str = "../../physics/flight_controllers/courbet/gazebo";
const QEMU_BUILD_DIR: &str = "../../../../qemu/build";
const MAV_C2_DIR: &str = "../../physics/flight_controllers/courbet/mavlink";

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

pub fn execute_mission(input: &TargetInput, param_names: String, timeout: f64) -> ExitKind {

    // 1. Apply inputs
    // println!("execute_mission received inputs: {:?}", input);
    let raw_paramater_values = input.get_param_bytes().target_bytes().clone();
    let raw_env_config = input.get_env_config();

    // Writing raw parameter values to a binary file for mav_c2 to read
    let mut bin_param_file: File = File::create("/tmp/mutations.bin").unwrap();
    let _ = bin_param_file.write_all(&raw_paramater_values).unwrap();
    let _ = bin_param_file.sync_all().unwrap();
    drop(bin_param_file);

    // TODO: Apply environment input

    // 2. Start Courbet services
    let spawn_services = Command::new("./run_and_attach_services.sh")
        .current_dir(RUN_SERVICES_DIR)
        .arg("rover")
        .stdout(Stdio::null())
        .spawn();
    if spawn_services.is_err() {
        panic!("Error: Failed to start services: {}", spawn_services.err().unwrap());
    }

    std::thread::sleep(std::time::Duration::from_secs(3));

    let mut spawn_c2 = Command::new("python3")
        .current_dir(MAV_C2_DIR)
        .arg("ardu_mav_c2.py")
        .arg("rover_init.param")
        .arg("rover_rollover_obtuse.txt")
        .arg(param_names)
        .arg("/tmp/mutations.bin")
        // .stdout(Stdio::null())
        .spawn();
    if spawn_c2.is_err() {
        panic!("Error: Failed to start ardu_mav_c2.py: {}", spawn_c2.err().unwrap());
    }

    std::thread::sleep(std::time::Duration::from_secs(3));

    let spawn_fd = Command::new("bash")
        .current_dir(QEMU_BUILD_DIR)
        .arg("../roverv462.sh")
        .stdout(Stdio::null())
        .spawn();
    if spawn_fd.is_err() {
        panic!("Error: Failed to start FastDyn QEMU: {}", spawn_fd.err().unwrap());
    }

    /*
        BUG: In the middle of the fuzzing campaign, at line 49 of run_and_attach_services.sh, a segfault sometimes occurs:
        ./run_and_attach_services.sh: line 49: 2748144 Segmentation fault      ./services "$MODEL_NAME"
        
        Temporary fix: the calls to the gazebo services will fail if this segfault occurs. When that happens,
        we will just return with ExitKind::Ok and let the fuzzer continue. 
     */

    // 3. Monitor for end states in order of priority
    let mut exit_kind: ExitKind;
    let mut node: Node = Node::new().unwrap();
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
    kill.wait();

    // Clean up zombies
    spawn_services.unwrap().wait();
    spawn_c2.unwrap().wait();
    spawn_fd.unwrap().wait();

    // TODO: Add deserialization of coverage here.
    deserialize_coverage("./covg.csv");

    println!("Mission execution finished with exit kind: {:?}", exit_kind);
    exit_kind
}