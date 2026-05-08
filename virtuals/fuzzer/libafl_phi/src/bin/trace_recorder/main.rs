use baby_fuzzer::gz_state_parser::{
    extract_block_from_gz_data, get_nested_pose, get_raw_gz_data, get_raw_hf_status, get_sim_time,
};

use core::panic;
use gz_msgs::{
    clock::Clock, imu::IMU, magnetometer::Magnetometer, navsat::NavSat, pose::Pose, pose_v::Pose_V,
};
use gz_transport::Node;
use protobuf::text_format::parse_from_str;
use std::fs::{File, OpenOptions};
use std::io::BufWriter;
use std::{env, io::Write};

use std::f32::consts::PI;
use std::fs;
use std::io::Read;
use std::os::unix::net::UnixListener;
use std::path::Path;
use std::process::Command;
use std::sync::Mutex;
use std::thread;
use std::thread::sleep;
use std::time::Duration;

/*
{"clock": system {
  sec: 1775366283
  nsec: 286575912
}
real {
  sec: 7
  nsec: 601130627
}
sim {
  sec: 4
  nsec: 453000000
}
,"pose": pose {
  header {
    stamp {
      sec: 4
      nsec: 440000000
    }
    data {
      key: "frame_id"
      value: "odom"
    }
    data {
      key: "child_frame_id"
      value: "base_link"
    }
  }
  position {
    x: -7.090779937325762e-18
    y: -3.0748414773205622e-07
    z: 0.16999987200181371
  }
  orientation {
    x: 5.8034884261329948e-10
    y: -5.8034884534338562e-10
    z: 0.70710678118655013
    w: 0.70710678118654491
  }
}
,"navsat": header {
  stamp {
    sec: 4
    nsec: 400000000
  }
  data {
    key: "seq"
    value: "22"
  }
}
latitude_deg: 35.363261999997249
longitude_deg: 149.165237
altitude: 584.169999874197
velocity_east: 1.1071559849587416e-18
velocity_north: 4.9747242202902807e-14
velocity_up: 1.7090292722771927e-11
frame_id: "skywalker_x8::base_link::navsat_sensor"
,"imu": header {
  stamp {
    sec: 4
    nsec: 452000000
  }
  data {
    key: "frame_id"
    value: "skywalker_x8::imu_link::imu_sensor"
  }
  data {
    key: "seq"
    value: "4451"
  }
}
entity_name: "skywalker_x8::imu_link::imu_sensor"
orientation {
  x: -2.8854485472995917e-18
  y: -9.5009347498588359e-10
  z: 3.7747582837255322e-15
  w: 1
}
orientation_covariance {
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
}
angular_velocity {
  x: -7.05935191322029e-18
  y: 4.0892886543327303e-13
  z: -7.51008420037562e-19
}
angular_velocity_covariance {
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
}
linear_acceleration {
  x: 1.9998108972612033e-08
  y: 1.2968626078519632e-15
  z: 9.7999999978676975
}
linear_acceleration_covariance {
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
  data: 0
}
,"mag": header {
  stamp {
    sec: 4
    nsec: 440000000
  }
  data {
    key: "frame_id"
    value: "skywalker_x8::base_link::magnetometer_sensor"
  }
  data {
    key: "seq"
    value: "105"
  }
}
field_tesla {
  x: -0.025089421621482144
  y: -0.29511201381683327
  z: 0.3239476085188866
}
}
 */

/**
 * Given a quaternion (x, y, z, w), return the roll, pitch, and yaw angles in radians.
 */
fn attitude_from_quat(x: f64, y: f64, z: f64, w: f64) -> (f64, f64, f64) {
    // Roll (x-axis rotation)
    let sinr_cosp = 2.0 * (w * x + y * z);
    let cosr_cosp = 1.0 - 2.0 * (x * x + y * y);
    let roll = sinr_cosp.atan2(cosr_cosp);

    // Pitch (y-axis rotation)
    let mut sinp = 2.0 * (w * y - z * x);

    // Clamp to handle numerical error
    if sinp > 1.0 {
        sinp = 1.0;
    } else if sinp < -1.0 {
        sinp = -1.0;
    }
    let pitch = sinp.asin();

    // Yaw (z-axis rotation)
    let siny_cosp = 2.0 * (w * z + x * y);
    let cosy_cosp = 1.0 - 2.0 * (y * y + z * z);
    let yaw = siny_cosp.atan2(cosy_cosp);

    (roll, pitch, yaw)
}

/**
 * Given the raw Gazebo data captured at a time step, extract the data and
 * write it as a line in the CSV file.
 */
fn record_state_at_time(gz_data: &str, file: &File) {
    static TIME: Mutex<Option<f64>> = Mutex::new(None);

    // First, extract all the blocks from the raw gz data
    let clock_block: String = extract_block_from_gz_data(gz_data, "clock");
    let pose_v_block: String = extract_block_from_gz_data(gz_data, "pose");
    let navsat_block: String = extract_block_from_gz_data(gz_data, "navsat");
    let imu_block: String = extract_block_from_gz_data(gz_data, "imu");
    // let magnetometer_block: String = extract_block_from_gz_data(gz_data, "mag");

    // Parse the blocks into their respective protobuf messages
    // pose comes as a Pose_V with one Pose inside, so just get the Pose at index 0
    let pose_v_proto: Pose_V = parse_from_str::<Pose_V>(&pose_v_block).unwrap();
    let pose_proto: Pose = pose_v_proto.pose[0].clone();
    let navsat_proto: NavSat = parse_from_str::<NavSat>(&navsat_block).unwrap();
    let imu_proto: IMU = parse_from_str::<IMU>(&imu_block).unwrap();
    // let magnetometer_proto: Magnetometer = parse_from_str::<Magnetometer>(&magnetometer_block).unwrap();

    // Extract data from blocks
    let mut sim_time = get_sim_time(&clock_block);

    let mut time_guard = TIME.lock().unwrap();

    let base_time = match *time_guard {
        Some(t) => t,
        None => {
            *time_guard = Some(sim_time);
            sim_time
        }
    };

    sim_time -= base_time;

    // Pose: position coords
    let x_pos = pose_proto.position.x;
    let y_pos = pose_proto.position.y;
    let z_pos = pose_proto.position.z;

    // Pose: attitude
    let x_quat = pose_proto.orientation.x;
    let y_quat = pose_proto.orientation.y;
    let z_quat = pose_proto.orientation.z;
    let w_quat = pose_proto.orientation.w;
    let (roll, pitch, yaw) = attitude_from_quat(x_quat, y_quat, z_quat, w_quat);

    // Navsat: linear velocity
    let lin_velo_x = navsat_proto.velocity_north;
    let lin_velo_y = navsat_proto.velocity_east;
    let lin_velo_z = navsat_proto.velocity_up;

    // IMU: angular velocity
    let ang_velo_x = imu_proto.angular_velocity.x; // roll rate
    let ang_velo_y = imu_proto.angular_velocity.y; // pitch rate
    let ang_velo_z = imu_proto.angular_velocity.z; // yaw rate

    // IMU: linear acceleration
    let lin_accel_x = imu_proto.linear_acceleration.x;
    let lin_accel_y = imu_proto.linear_acceleration.y;
    let lin_accel_z = imu_proto.linear_acceleration.z;

    // Now, write everything to a line in the CSV file
    let mut writer: BufWriter<&File> = BufWriter::new(&file);
    write!(writer, "{sim_time},");
    write!(writer, "{x_pos},{y_pos},{z_pos},");
    write!(writer, "{roll},{pitch},{yaw},");
    write!(writer, "{lin_velo_x},{lin_velo_y},{lin_velo_z},");
    write!(writer, "{ang_velo_x},{ang_velo_y},{ang_velo_z},");
    write!(writer, "{lin_accel_x},{lin_accel_y},{lin_accel_z}");
    write!(writer, "\n");

    writer.flush().unwrap();
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 5 {
        println!(
            "Usage: {} execution_num time_step sim_time_limit trace_log_dir",
            args[0]
        );
        return Err("Not enough arguments provided".into());
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

    // Record data until time is up!
    let mut node: Node = Node::new().unwrap();
    let mut current_sim_time: f64 = 0.0;
    // Enforce a standard recording start of t+5 seconds to mitigate start time deviations
    // Mission will never start this quick anyway
    let recording_start_time: f64 = 5.0;
    let mut next_time_to_record: f64 = recording_start_time;

    // wait for arm throttle
    let socket_path = std::env::var("FASTDYN_OPTIFUZZ_SOCKET")
        .unwrap_or_else(|_| "/tmp/rust_receiver.sock".to_string());

    // Remove stale socket file if it exists
    if Path::new(&socket_path).exists() {
        fs::remove_file(&socket_path)?;
    }

    let listener = UnixListener::bind(&socket_path)?;
    println!("Rust server listening on {}", socket_path);

    // Set nonblocking
    listener.set_nonblocking(true)?;

    loop {
        match listener.accept() {
            Ok((mut stream, _addr)) => {
                println!("Client connected");

                let mut buf = [0u8; 1024];
                let n = match stream.read(&mut buf) {
                    Ok(size) => size,
                    Err(e) => {
                        println!("Failed to read from client: {}", e);
                        continue;
                    }
                };

                if n > 0 {
                    let msg = String::from_utf8_lossy(&buf[..n]);
                    println!("Received: {}", msg);

                    if msg.trim() == "throttle armed" {
                        println!("Throttle armed message received, starting recording...");
                        break;
                    } else {
                        println!("Received unexpected message: {}", msg);
                    }
                } else {
                    println!("Client connected but sent no data");
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                println!("No client yet, polling...");
                thread::sleep(Duration::from_millis(500));
            }
            Err(e) => return Err(Box::new(e)),
        }
    }

    let _ = fs::remove_file(&socket_path);

    loop {
        let gz_data: String = get_raw_gz_data(&mut node);
        if gz_data.is_empty() {
            // Service may not be ready, try again
            continue;
        }

        let clock_block: String = extract_block_from_gz_data(&gz_data, "clock");
        current_sim_time = get_sim_time(&clock_block);
        if current_sim_time >= sim_time_limit {
            break;
        // } else if current_sim_time < recording_start_time {
        //     continue;
        } else if current_sim_time >= next_time_to_record {
            record_state_at_time(&gz_data, &file);
            next_time_to_record += time_step;
        }
    }

    return Ok(());
}
