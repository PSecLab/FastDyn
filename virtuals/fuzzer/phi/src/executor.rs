use std::process::{Child, Command, Stdio};

pub struct Executor {
    target: Command,
    child_process: Option<Child>,
}

impl Executor {

    pub fn new() -> Self {
        let mut cmd = Command::new("bash");
        cmd.arg("-lc")
            .arg("/home/ere/fire/PRehost/gazebo/my_ackermann_w_state.sh")
            .stdout(Stdio::inherit())
            .stderr(Stdio::inherit());

        Self { 
            target: cmd,
            child_process: None,
        }
    }

    pub fn execute_target(&mut self) {
        self.child_process = Some(self.target.spawn().expect("Failed to start target process"));
    }

    pub fn kill_target(&mut self) {

        if let Some(child) = self.child_process.as_mut() {
            child.kill().expect("Failed to kill target process");
        }

        Command::new("pkill")
            .arg("-f")
            .arg("gz")
            .output()
            .expect("Failed to kill gz process");

        Command::new("pkill")
            .arg("-f")
            .arg("state_service")
            .output()
            .expect("Failed to kill state_service process");

    }
}