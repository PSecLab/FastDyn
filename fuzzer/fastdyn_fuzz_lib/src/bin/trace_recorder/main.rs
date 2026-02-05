use fastdyn_fuzzer::gz_state_parser::{
    get_raw_gz_data, 
    get_raw_hf_status, 
    extract_block_from_gz_data, 
    get_nested_pose,
    get_sim_time
};

use core::panic;
use std::{env, io::Write};
use std::fs::{self, File, OpenOptions};
use std::io::BufWriter;
use protobuf::text_format::parse_from_str;
use gz_transport::Node;
use gz_msgs::{
    clock::Clock, imu::IMU, magnetometer::Magnetometer, navsat::NavSat, pose::Pose, pose_v::Pose_V
};

use std::process::Command;
use std::thread::sleep;

/*
    Writes a single line representing the state of the simulated CPS to the CSV file.
    Returns the sim time at which the state was recorded.
*/
fn record_state_at_time(gz_data: &str, file: &File) -> f64 {

    // First, extract all the blocks from the raw gz data
    let clock_block: String = extract_block_from_gz_data(gz_data, "clock");
    let pose_block: String = extract_block_from_gz_data(gz_data, "pose");
    println!("EXTRACTED POSE BLOCK: {}", pose_block);
    // let imu_block: String = extract_block_from_gz_data(gz_data, "imu");
    // let magnetometer_block: String = extract_block_from_gz_data(gz_data, "mag");
    // let navsat_block: String = extract_block_from_gz_data(gz_data, "navsat");

    // Parse the blocks into their respective protobuf messages

    // let pose_proto: Pose = get_nested_pose(pose_block, "pose");
    let pose_proto: Pose_V = parse_from_str::<Pose_V>(&pose_block).unwrap();
    let pose_proto: Pose = pose_proto.pose[0].clone();

    // let imu_proto: IMU = parse_from_str::<IMU>(&imu_block).unwrap();
    // let magnetometer_proto: Magnetometer = parse_from_str::<Magnetometer>(&magnetometer_block).unwrap();
    // let navsat_proto: NavSat = parse_from_str::<NavSat>(&navsat_block).unwrap();

    // Get all the relevant data fields
    let sim_time: f64 = get_sim_time(&clock_block);
    let x_pos: f64 = pose_proto.position.x;
    let y_pos: f64 = pose_proto.position.y;
    let z_pos: f64 = pose_proto.position.z;
    // TODO: Get everything else here!

    let mut writer: BufWriter<&File> = BufWriter::new(&file);
    writeln!(writer, "{},{},{},{}", sim_time, x_pos, y_pos, z_pos).unwrap();
    writer.flush().unwrap();

    sim_time

}

fn main() {

    // println!("Hello from trace_recorder!");
    // std::thread::sleep(std::time::Duration::from_secs(3));

    let args: Vec<String> = env::args().collect();

    if args.len() < 5 {
        println!("Usage: {} execution_num time_step sim_time_limit trace_log_dir", args[0]);
        return;
    }

    let execution_num: usize = args[1].parse().unwrap();

    let time_step: f64 = args[2].parse().unwrap();
    if time_step <= 0.0 {
        panic!("Error: time_step must be greater than 0.0!");
    }

    let sim_time_limit: f64 = args[3].parse().unwrap();
    if sim_time_limit <= 0.0 {
        panic!("Error: sim_time_limit must be greater than 0.0!");
    }

    // At this point, gazebo should be running (started by the executor).
    // We can start recording the trace.

    // First, create the trace log directory if it does not exist and the log file
    let trace_log_dir: &str = &args[4];
    if !fs::exists(trace_log_dir).unwrap() {
        // Create the directory if it does not exist
        fs::create_dir_all(trace_log_dir).unwrap();
    }
    let file_path: String = format!("{}/trace_{}.csv", trace_log_dir, execution_num);
    let file: File = OpenOptions::new()
        .write(true)
        .truncate(true)
        .create(true)
        .open(file_path)
        .unwrap();

    // Poll until we get a valid sim time from gazebo
    let mut node: Node = Node::new().unwrap();
    // println!("NODE CREATED");

    let mut current_sim_time: f64;
    loop {

        let gz_data: String = get_raw_gz_data(&mut node);

        println!("GOT GZ DATA: {gz_data}");

        if gz_data.is_empty() {
            continue;
        }

        let clock_block: String = extract_block_from_gz_data(&gz_data, "clock");
        let sim_time: f64 = get_sim_time(&clock_block);
        current_sim_time = sim_time;

        // println!("GOT SIM TIME: {}", current_sim_time);

        if current_sim_time == 0.0 {
            // Still not ready to record data, keep polling!
            continue;
        } else if current_sim_time >= sim_time_limit {
            // The simulation wasn't properly reset, time is already up!
            panic!("Error: Simulation was not properly reset before trace recording!");
        } else {
            // Record the first state of the trace, break into the main loop
            record_state_at_time(&gz_data, &file);
            std::thread::sleep(std::time::Duration::from_secs_f64(time_step));

            break;
        }
    }

    // println!("RECORDING MAIN LOOP");

    // Keep recording trace data until time is up!
    while current_sim_time + time_step <= sim_time_limit { // + time_step to account for sleep delay

        let gz_data: String = get_raw_gz_data(&mut node);
        if gz_data.is_empty() {
            panic!("Error: Failed to get gazebo data during trace recording!");
        }

        current_sim_time = record_state_at_time(&gz_data, &file);

        std::thread::sleep(std::time::Duration::from_secs_f64(time_step));
    };

}