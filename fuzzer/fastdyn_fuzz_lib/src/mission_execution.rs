use crate::inputs::TargetInput;
use baby_fuzzer::gz_state_parser::{
    get_raw_gz_data, 
    get_raw_hf_status, 
    extract_block_from_gz_data,
    get_sim_time,
};

use libafl::executors::ExitKind;
use std::process::{Command, Stdio, Child};
use std::fs::File;
use std::io::Write;
use gz_transport::Node;
use gz_msgs::clock::Clock

// ------------------------------
// Input your paths here...
const RUN_SERVICES_PATH: &str = "/root/fire/fuzz_testing/FastDyn/courbet/gazebo/";
const QEMU_BUILD_PATH: &str = "/root/fire/fuzz_testing/qemu/build";
const MAV_C2_PATH: &str = "/root/fire/fuzz_testing/FastDyn/courbet/mavlink/";
// ------------------------------

/*
    Steps:
    1. Apply inputs
    2. Start Courbet services
        - run_and_attach_services.sh
        - mav_command_and_control.py
        - fd_rover.sh
    3. Start noise injection service (TODO)
    4. Monitor for end states and return an ExitKind accordingly:
        - Mission complete (Mavlink C2 exits without error) = ExitKind::Ok
        - Crash (/get_hf_status says "true") = ExitKind::Crash
        - Timeout (sim time from /clock) = ExitKind::Timeout
*/

// pub fn execute_misison(input: &TargetInput, param_names: Vec<String>, timeout: f64) -> ExitKind {
pub fn execute_missioin() -> ExitKind {

    let timeout = 10.0;

    // println!("Hello from execute_mission!");
    // println!("Input: {:?}", input);
    // println!("Param names: {:?}", param_names);
    // println!("Timeout: {}", timeout);
    // ExitKind::Ok

    // 1. Apply inputs
    // let raw_paramater_values = input.get_param_bytes().target_bytes();
    // let raw_env_config = input.get_env_config();

    // let mut bin_param_file: File = File::create("/tmp/ardu_params.bin").unwrap();
    // let _ = bin_param_file.write_all(&raw_paramater_values).unwrap();
    // let _ = bin_param_file.sync_all().unwrap();
    // drop(bin_param_file);

    // 2. Start Courbet services
    let spawn_services = Command::new("./run_and_attach_services.sh")
        .current_dir(RUN_SERVICES_PATH)
        .arg("rover")
        .stdout(Stdio::null())
        .spawn();
    if spawn_services.is_err() {
        panic!("Error: Failed to start services: {}", spawn_services.err().unwrap());
    }

    let spawn_c2 = Command::new("python3")
        .current_dir(MAV_C2_PATH)
        .arg("mav_command_and_control.py")
        // .arg("/tmp/ardu_params.bin")
        // .arg(param_names)
        .arg("rover_init.parm")
        .stdout(Stdio::null())
        .spawn();
    if spawn_c2.is_err() {
        panic!("Error: Failed to start mav_command_and_control.py: {}", spawn_c2.err().unwrap());
    }

    let spawn_fd = Command::new("bash")
        .current_dir(QEMU_BUILD_PATH)
        .arg("../fd_rover.sh")
        .stdout(Stdio::null())
        .spawn();
    if spawn_fd.is_err() {
        panic!("Error: Failed to start FastDyn QEMU: {}", spawn_fd.err().unwrap());
    }

    // 3. Start noise injection service (TODO)
    
    // 4. Monitor for end states
    let mut exit_kind: ExitKind;
    let mut node: Node = Node::new().unwrap();
    loop {

        // Check for timeout
        let gz_data = get_raw_gz_data(&mut node);
        let clock_block = extract_block_from_gz_data(&gz_data, "clock");
        let sim_time: f64 = get_sim_time(&clock_block);
        if sim_time >= timeout {
            exit_kind = ExitKind::Timeout;
            break;
        } else {
            println!("Current sim time: {}", sim_time);
        }

        // Check for hard fault
        let hf_status: String = get_raw_hf_status(&mut node);
        if hf_status.find("true").is_some() {
            exit_kind = ExitKind::Crash;
            break;
        } else {
            println!("No hard fault detected.");
        }

        // Check for mission complete
        let status = spawn_c2.as_mut().unwrap().try_wait().unwrap();
        if status.is_some() {
            // Mavlink C2 has exited. If the exit code is 0, mission complete.
            if status.unwrap().success() {
                exit_kind = ExitKind::Ok;
                break;
            } 
        } else {
            println!("Mavlink C2 still running.");
        }

    }

    // Kill processes (default is SIGKILL)
    let _ = spawn_services.kill().unwrap();
    let _ = spawn_c2.kill().unwrap();
    let _ = spawn_fd.kill().unwrap();

    println!("Mission execution finished with exit kind: {:?}", exit_kind);
    exit_kind

}