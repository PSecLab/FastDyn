use core::panic;
use std::time::Duration;
use gz_transport::Node;
use protobuf::text_format::parse_from_str;
use gz_msgs::{pose::Pose, pose_v::Pose_V};
use gz_msgs::gz_msgs10::{
    any::Any, empty::Empty,
};

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
        panic!("Error: Service call to {} failed!", service_name);
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