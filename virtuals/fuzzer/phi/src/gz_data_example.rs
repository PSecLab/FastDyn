mod gz_state_parser;

use protobuf::text_format::{ParseError, parse_from_str};
use gz_msgs::{clock, gz_msgs10::{
    any::Any, 
    clock::Clock, 
    empty::Empty, 
    imu::IMU, 
    magnetometer::Magnetometer, 
    navsat::NavSat, 
    pose_v::Pose_V,
}};

fn example_proto_parsing(gz_data: &str) {
    let clock_str: String = gz_state_parser::extract_block_from_gz_data(&gz_data, "clock");
    let clock_proto: Clock = parse_from_str::<Clock>(&clock_str).unwrap();
    let sim_sec: i64 = clock_proto.sim.sec;
    let sim_nsec: i32 = clock_proto.sim.nsec;
    println!("Simulation time: {}.{} seconds", sim_sec, sim_nsec);

    let imu_str: String = gz_state_parser::extract_block_from_gz_data(&gz_data, "imu");
    let imu_proto: IMU = parse_from_str::<IMU>(&imu_str).unwrap();
    let lin_acc_x: f64 = imu_proto.linear_acceleration.x;
    let lin_acc_y: f64 = imu_proto.linear_acceleration.y;
    let lin_acc_z: f64 = imu_proto.linear_acceleration.z;
    println!("IMU Linear Acceleration: ");
    println!("\tx: {lin_acc_x}");
    println!("\ty: {lin_acc_y}");
    println!("\tz: {lin_acc_z}");

    let pose_str: String = gz_state_parser::extract_block_from_gz_data(&gz_data, "pose");
    let pose_proto: Pose_V = parse_from_str::<Pose_V>(&pose_str).unwrap();
    for (_i, pose) in pose_proto.pose.iter().enumerate() {
        if pose.name == "vehicle" {
            let orientation = &pose.orientation;
            println!("Rover Orientation: ");
            println!("\tx: {}", orientation.x);
            println!("\ty: {}", orientation.y);
            println!("\tz: {}", orientation.z);
            println!("\tw: {}", orientation.w);
            break;
        }
    }
}

fn main() {
    // Order of blocks: clock, pose, navsat, imu, magnetometer
    let gz_data: String = gz_state_parser::get_raw_gz_data();
    println!("{}", gz_data);
    println!("-----------------------------------");
    example_proto_parsing(&gz_data);
}