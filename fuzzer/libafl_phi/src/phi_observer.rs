use libafl::observers::Observer;
use libafl::executors::ExitKind;
use libafl::Error;
use libafl_bolts::Named;
use core::f64;
use std::process::{Command, Child};
use std::borrow::Cow;
use serde::{Deserialize, Serialize};

use banquo::{EvaluationError, Formula, Trace, evaluate, predicate, trace};
use banquo::operators::{Always, And, Not, Implies};


#[derive(Serialize, Deserialize, Debug)]
pub struct PhysicalObserver {
    name: Cow<'static, str>,
    trace_time_step: f64, // Time step between recorded trace points
    sim_time_limit: f64, // Maximum simulation time to record
    trace_log_dir: String, // Directory to store the trace logs
    latest_robustness_vec: Vec<f64>, // Vector to store latest robustness values for each STL formula

    #[serde(skip)]
    recorder_process: Option<Child>, // Child process handle for the recorder
}

impl Named for PhysicalObserver {
    fn name(&self) -> &Cow<'static, str> {
        &self.name
    }
}

impl PhysicalObserver {

    pub fn new(step: f64, limit: f64, log_dir: &str) -> Self {
        
        let step: f64 = if step <= 0.0 { 0.1 } else { step };
        let limit: f64 = if limit <= 0.0 { 10.0 } else { limit };

        let mut robustness_vec: Vec<f64> = Vec::new();
        // TODO: Push as many infinity values as there are formulas being tracked
        robustness_vec.push(f64::INFINITY); // Placeholder for now

        Self {
            name: Cow::from("PhysicalObserver"),
            trace_time_step: step,
            sim_time_limit: limit,
            trace_log_dir: String::from(log_dir),
            latest_robustness_vec: robustness_vec,
            recorder_process: None,
        }
    }

    pub fn get_robustness_vec(&self) -> &Vec<f64> {
        &self.latest_robustness_vec
    }

}

impl<I, S> Observer<I, S> for PhysicalObserver {

    fn pre_exec(&mut self, _state: &mut S, _input: &I) -> Result<(), Error> {

        // println!("Hello from PhysicalObserver PRE_exec!");
        // self.latest_robustness_vec[0] = self.count as f64;
        
        // TODO: Start the recorder binary here

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

        // println!("Hello from PhysicalObserver POST_exec!");


        // TODO: Stop the recorder binary(?), compute robustness values

        Ok(())
    }
}