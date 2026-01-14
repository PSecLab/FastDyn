use libafl::observers::Observer;
use libafl::executors::ExitKind;
use libafl::Error;
use libafl_bolts::Named;

// use std::process::{Command, Child};
use std::borrow::Cow;
use serde::{Deserialize, Serialize};


#[derive(Serialize, Deserialize, Debug)]
pub struct PhysicalObserver {
    name: Cow<'static, str>,
    trace_time_step: f64, // Time step between recorded trace points
    sim_time_limit: f64, // Maximum simulation time to record
    recorder_bin_path: String, // Path to the external recorder binary
    trace_log_dir: String, // Directory to store the trace logs
    count: u64,
    // recorder_process: Option<Child>, // Child process handle for the recorder
}

impl Named for PhysicalObserver {
    fn name(&self) -> &Cow<'static, str> {
        &self.name
    }
}

impl PhysicalObserver {
    pub fn new(step: f64, limit: f64, bin_path: &str, log_dir: &str) -> Self {
        
        let step: f64 = if step <= 0.0 { 0.1 } else { step };
        let limit: f64 = if limit <= 0.0 { 10.0 } else { limit };

        Self {
            name: Cow::from("PhysicalObserver"),
            trace_time_step: step,
            sim_time_limit: limit,
            recorder_bin_path: String::from(bin_path),
            trace_log_dir: String::from(log_dir),
            count: 0,

            //recorder_process: None,
        }
    }
}

impl<I, S> Observer<I, S> for PhysicalObserver {

    fn pre_exec(&mut self, _state: &mut S, _input: &I) -> Result<(), Error> {

        println!("Hello from PhysicalObserver pre_exec! Count = {}", self.count);
        self.count += 1;
        
        // if self.recorder_process.is_none() {

        //     self.recorder_process = Command::new(&self.recorder_bin_path)
        //         .args([self.trace_time_step.to_string(), 
        //             self.sim_time_limit.to_string(), 
        //             self.trace_log_dir.clone()
        //         ])   
        //         .spawn()
        //         .ok();
        // }

        Ok(())
    }

    fn post_exec(&mut self, _state: &mut S, _input: &I, _exit_kind: &ExitKind,) -> Result<(), Error> {

        // TODO: post_exec() isn't very useful right now since the recorder just needs
        // to be started, and then it stops itself automatically.
        // Maybe we can do some trace post-processing here?

        Ok(())
    }
}