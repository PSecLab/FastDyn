use baby_fuzzer::gz_state_parser::get_raw_hf_status;
use gz_transport::Node;
use std::{env, io::Write};
use std::fs::{self, File, OpenOptions};

fn main() {

    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        println!("Usage: hf_detector /path/to/fault/log/dir");
        return;
    }

    let hf_log_dir: &str = &args[1];
    if !fs::exists(hf_log_dir).unwrap() {
        // Create the directory if it does not exist
        fs::create_dir_all(hf_log_dir).unwrap();
    }
    let file_path: String = format!("{}/hardfaults.txt", hf_log_dir);
    let file: File = OpenOptions::new()
        .write(true)
        .truncate(true)
        .create(true)
        .open(file_path)
        .unwrap();

    let mut node: Node = Node::new().unwrap();
    loop {
        let hf_status: String = get_raw_hf_status(&mut node);
        if hf_status.find("true").is_some() {
            println!("HARD FAULT DETECTED: {}", hf_status);
            let mut file = file.try_clone().unwrap();
            writeln!(file, "{}", hf_status).unwrap();
            break;
        } else {
            println!("No hard fault detected yet.");
        }
        std::thread::sleep(std::time::Duration::from_millis(1000));
    }

}