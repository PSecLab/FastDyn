use std::time::Duration;
use gz_transport::Node;
use gz_msgs::{gz_msgs10::{
    empty::Empty,
    any::Any,
}};

pub fn get_raw_gz_data() -> String {
    let mut node: Node = Node::new().unwrap();
    let request: Empty = Empty::new();
    let timeout: u64 = 5000; // milliseconds
    let service_name: &str = "/get_trace_state";

    let (response, result): (Any,bool) = node.request::<Empty, Any>(
        service_name, &request, Duration::from_millis(timeout)
        ).unwrap();

    if result {
        println!("Service call successful!");
        response.string_value().to_string()
    } else {
        println!("Service call failed.");
        String::new()
    }
}

pub fn extract_block_from_gz_data(gz_data: &str, block_name: &str) -> String {
    let start_index: usize = gz_data.find(block_name).unwrap();
    let comma_index_opt: Option<usize>= gz_data[start_index..].find(",");
    let end_index: usize = match comma_index_opt {
        Some(idx) => idx + start_index + 1,
        None => gz_data.len(), // Final block won't have a comma
    };

    let block: String = gz_data[(start_index - 1)..(end_index - 2)].to_string();
    // Need to remove the "[block_name]": to parse as a protobuf
    let colon_index: usize = block.find(":").unwrap();
    
    block[colon_index + 1..].trim().to_string()
}