use std::process::{Command, Child};

/*
    In LibAFL, an observer object examines the state of the target after each execution.
    In phi's case, the observer will help record the physical trace of the simulated CPS.
    Observers in LibAFL have two main functions: pre_exec() and post_exec().
*/

pub struct Observer {
    trace_time_step: f64, // Time step between recorded trace points
    sim_time_limit: f64, // Maximum simulation time to record
    recorder_bin_path: String, // Path to the external recorder binary
    trace_log_dir: String, // Directory to store the trace logs
    recorder_process: Option<Child>, // Child process handle for the recorder
}

impl Observer{

    pub fn new(step: f64, limit: f64, bin_path: &str, log_dir: &str) -> Self {
        
        let step: f64 = if step <= 0.0 { 0.1 } else { step };
        let limit: f64 = if limit <= 0.0 { 10.0 } else { limit };

        Self {
            trace_time_step: step,
            sim_time_limit: limit,
            recorder_bin_path: String::from(bin_path),
            trace_log_dir: String::from(log_dir),
            recorder_process: None,
        }
    }

    pub fn pre_exec(&mut self) -> bool{

        if self.recorder_process.is_none() {

            self.recorder_process = Command::new(&self.recorder_bin_path)
                .args([self.trace_time_step.to_string(), 
                    self.sim_time_limit.to_string(), 
                    self.trace_log_dir.clone()
                ])   
                .spawn()
                .ok();

            self.recorder_process.is_some()

        } else {
            false
        }
    }

    pub fn post_exec(&mut self) -> bool{

        // TODO: post_exec() isn't very useful right now since the recorder just needs
        // to be started, and then it stops itself automatically.
        // Maybe we can do some trace post-processing here?

        if self.recorder_process.is_some() {

            let result: bool = self.recorder_process.as_mut().unwrap().kill().is_ok();
            self.recorder_process = None;
            result

        } else {
            false
        }
    }

}