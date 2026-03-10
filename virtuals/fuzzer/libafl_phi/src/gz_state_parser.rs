use core::panic;
use std::time::Duration;
use gz_transport::Node;
use protobuf::text_format::parse_from_str;
use gz_msgs::{pose::Pose, pose_v::Pose_V, clock::Clock};
use gz_msgs::gz_msgs10::{
    any::Any, empty::Empty, stringmsg::StringMsg, boolean::Boolean
};

fn set_noise(node: &mut Node, noise_type: &str, white_scale: f64, drift_scale: f64) {

    let request_str = format!("{noise_type},{white_scale},{drift_scale}");
    let mut request = StringMsg::new();
    request.data = request_str;
    let timeout: u64 = 3000; // milliseconds
    let service_name: &str = "/set_noise_scale";

    let mut response_opt: Option<(Boolean, bool)> = None;
    for attempt in 0..5 {
        response_opt = node.request::<StringMsg, Boolean>(
            service_name, &request, Duration::from_millis(timeout));
        if response_opt.is_some() {
            break;
        } else {
            println!("Service call to {} failed, retrying... ({}/5)", service_name, attempt + 1);
        }
    }

    // return the result
    if let Some((_, success)) = response_opt {
        println!("Applied {} noise with white scale {} and drift scale {}.", noise_type, white_scale, drift_scale);
    } else {
        println!("FAILED to apply {} noise.", noise_type);
    }
}

pub fn apply_noise(raw_env_config: String, node: &mut Node, sim_time: f64) {

    if raw_env_config.is_empty() {
        return;
    }

    // We need at least one parameter
    if !raw_env_config.contains("gyro_white_scale") && !raw_env_config.contains("gyro_drift_scale") && !raw_env_config.contains("accel_white_scale") && !raw_env_config.contains("accel_drift_scale") && !raw_env_config.contains("mag_white_scale") && !raw_env_config.contains("mag_drift_scale") {
        return;
    }

    let mut gyro_white_scale = 0.0;
    let mut gyro_drift_scale = 0.0;
    let mut accel_white_scale = 0.0;
    let mut accel_drift_scale = 0.0;
    let mut mag_white_scale = 0.0;
    let mut mag_drift_scale = 0.0;

    // Extract values conditionally
    if let Some(value) = raw_env_config.split("gyro_white_scale:").nth(1) {
        gyro_white_scale = value.split(',').nth(0).unwrap_or("0.0").parse().unwrap_or(0.0);
    }
    if let Some(value) = raw_env_config.split("gyro_drift_scale:").nth(1) {
        gyro_drift_scale = value.split(',').nth(0).unwrap_or("0.0").parse().unwrap_or(0.0);
    }
    if let Some(value) = raw_env_config.split("accel_white_scale:").nth(1) {
        accel_white_scale = value.split(',').nth(0).unwrap_or("0.0").parse().unwrap_or(0.0);
    }
    if let Some(value) = raw_env_config.split("accel_drift_scale:").nth(1) {
        accel_drift_scale = value.split(',').nth(0).unwrap_or("0.0").parse().unwrap_or(0.0);
    }
    if let Some(value) = raw_env_config.split("mag_white_scale:").nth(1) {
        mag_white_scale = value.split(',').nth(0).unwrap_or("0.0").parse().unwrap_or(0.0);
    }
    if let Some(value) = raw_env_config.split("mag_drift_scale:").nth(1) {
        mag_drift_scale = value.split(',').nth(0).unwrap_or("0.0").parse().unwrap_or(0.0);
    }

    // Send the noise
    let gyro_result = set_noise(node, "gyro", gyro_white_scale, gyro_drift_scale);
    let accel_result = set_noise(node, "accel", accel_white_scale, accel_drift_scale);
    let mag_result = set_noise(node, "mag", mag_white_scale, mag_drift_scale);

    println!("Applied noise at sim time {} seconds.", sim_time);
}

/**
 * After making repeated, rapid succession service calls over the course of a fuzzing campaign,
 * sometimes Gazebo services become unresponsive. Maybe if we reset the node we can repair it?
 */
pub fn reset_gz_node(node: &mut Node) {
    println!("Resetting Gazebo transport node in an attempt to repair it...");
    *node = Node::new().unwrap();
    std::thread::sleep(std::time::Duration::from_secs(3));
}

/*
    Use the /get_trace_state service to get the raw gz data as a string.
    Returns String::new() if the service call fails.
*/
pub fn get_raw_gz_data(node: &mut Node) -> String {

    // let mut node: Node = Node::new().unwrap();
    let request: Empty = Empty::new();
    let timeout: u64 = 3000; // milliseconds
    let service_name: &str = "/get_trace_state";

    let mut response_opt: Option<(Any, bool)> = None;
    for attempt in 0..5 {
        response_opt = node.request::<Empty, Any>(
        service_name, &request, Duration::from_millis(timeout));
        if response_opt.is_some() {
            break;
        } else {
            println!("Service call to {} failed, retrying... ({}/5)", service_name, attempt + 1);
        }
    }
    
    if response_opt.is_none() {
        // panic!("Error: Service call to {} failed!", service_name);
        println!("Warning: Service call to {} failed!", service_name);
        // reset_gz_node(node);
        return String::new();
    }

    let (response, result): (Any, bool) = response_opt.unwrap();

    if result {
        // println!("Service call successful!");
        response.string_value().to_string()
    } else {
        // println!("Error: Service call to {} returned failure.", service_name);
        String::new()
    }
}

/*
    Parses an extracted clock block string from gazebo data and returns the sim time in seconds.
*/
pub fn get_sim_time(clock_block: &str) -> f64 {
    let clock_proto: Clock = parse_from_str::<Clock>(clock_block).unwrap();
    let sim_sec: i64 = clock_proto.sim.sec;
    let sim_nsec: i32 = clock_proto.sim.nsec;
    (sim_sec as f64) + (sim_nsec as f64) * 1e-9
}

pub fn get_raw_hf_status(node: &mut Node) -> String {

    let request: Empty = Empty::new();
    let timeout: u64 = 3000; // milliseconds
    let service_name: &str = "/get_hf_status";

    let mut response_opt: Option<(StringMsg, bool)> = None;
    for attempt in 0..5 {
        response_opt = node.request::<Empty, StringMsg>(
        service_name, &request, Duration::from_millis(timeout));
        if response_opt.is_some() {
            break;
        } else {
            println!("Service call to {} failed, retrying... ({}/5)", service_name, attempt + 1);
        }
    }
    
    if response_opt.is_none() {
        println!("Warning: Service call to {} failed!", service_name);
        // reset_gz_node(node);
        return String::new();
    }

    let (response, result): (StringMsg, bool) = response_opt.unwrap();

    if result {
        // println!("Service call successful!");
        response.data
    } else {
        // println!("Error: Service call to {} returned failure.", service_name);
        String::new()
    }
}

/*
    Parse and return a specific block from the raw gz data string.
    Returns String::new() if the block is not found.
*/
pub fn extract_block_from_gz_data(gz_data: &str, block_name: &str) -> String {

    let start_index_opt: Option<usize> = gz_data.find(block_name);

    if start_index_opt.is_none() {
        return String::new();
    }

    let start_index: usize = start_index_opt.unwrap();
    let comma_index_opt: Option<usize>= gz_data[start_index..].find(",");

    let end_index: usize = if comma_index_opt.is_none() {
        // Final block won't have a comma, just make it the end of the string
        gz_data.len()
    } else {
        comma_index_opt.unwrap() + start_index + 1
    };

    let block: String = gz_data[(start_index - 1)..(end_index - 2)].to_string();
    // Need to remove the "[block_name]": to parse as a protobuf
    let colon_index: usize = block.find(":").unwrap();
    
    block[colon_index + 1..].trim().to_string()
}

/*
    From an extracted pose block string, parse and return the nested pose with the given name.
    Returns Pose::new() if the pose is not found.
*/
pub fn get_nested_pose(extracted_pose_block: String, pose_name: &str) -> Pose {

    let pose_proto: Pose_V = parse_from_str::<Pose_V>(&extracted_pose_block).unwrap();
    for (_i, pose) in pose_proto.pose.iter().enumerate() {
        if pose.name == pose_name {
            return pose.clone();
        }
    }

    Pose::new()
}