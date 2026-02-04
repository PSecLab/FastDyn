use libafl::observers::Observer;
use libafl::executors::ExitKind;
use libafl::state::HasExecutions;
use libafl::Error;
use libafl_bolts::Named;

use core::f64;
use std::process::{Command, Child};
use std::borrow::Cow;
use std::thread::sleep;
use std::time::Duration;
use serde::{Deserialize, Serialize};
use csv::Reader;
use std::collections::HashMap;

use banquo::{EvaluationError, Formula, Trace, evaluate, predicate};
use banquo::operators::{Always, And, Eventually, Implies, Not, Or};
use banquo::predicate::FormulaError;

use std::fs::{File, OpenOptions, create_dir_all};
use std::io::Write;
use std::path::Path;

use crate::cpexp_state::{HasLatestRobustness, HasOptimizer};

// Example: Capturing timestamp, x, y, z coords
#[derive(Deserialize)]
struct State {
    time: f64,
    x: f64,
    y: f64,
    z: f64,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct PhysicalObserver {
    name: Cow<'static, str>,
    trace_time_step: f64, // Time step between recorded trace points
    sim_time_limit: f64, // Maximum simulation time to record
    trace_log_dir: String, // Directory to store the trace logs
    robustness_log_dir: String, // Directory to store the robustness logs
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

    pub fn new(step: f64, limit: f64, trace_log_dir: &str, robustness_log_dir: &str) -> Self {
        
        let step: f64 = if step <= 0.0 { 0.1 } else { step };
        let limit: f64 = if limit <= 0.0 { 10.0 } else { limit };

        let mut robustness_vec: Vec<f64> = Vec::new();
        // TODO: Push as many infinity values as there are formulas being tracked
        robustness_vec.push(f64::INFINITY); // Placeholder for now

        Self {
            name: Cow::from("PhysicalObserver"),
            trace_time_step: step,
            sim_time_limit: limit,
            trace_log_dir: String::from(trace_log_dir),
            robustness_log_dir: String::from(robustness_log_dir),
            latest_robustness_vec: robustness_vec,
            recorder_process: None,
        }
    }

    pub fn get_robustness_vec(&self) -> &Vec<f64> {
        &self.latest_robustness_vec
    }

}

impl<I, S> Observer<I, S> for PhysicalObserver 
where
    S: HasExecutions + HasOptimizer + HasLatestRobustness, 
{

    fn pre_exec(&mut self, state: &mut S, _input: &I) -> Result<(), Error> {

        println!("Hello from PhysicalObserver PRE_exec!");
        // self.latest_robustness_vec[0] = self.count as f64;
        
        if self.recorder_process.is_some() {
            // The recorder process should be none...
            panic!("Error: Recorder process is NOT none in pre_exec!");
        }

        // Nifty way to get the path of ./trace_recorder no matter where you run ./baby_fuzzer
        let mut recorder_path = std::env::current_exe().expect("Failed to get current executable path");
        recorder_path.pop(); // remove baby_fuzzer filename
        recorder_path.push("trace_recorder");

        let spawn_result = Command::new(recorder_path)
             .args([(state.executions() + 1).to_string(),
                self.trace_time_step.to_string(), 
                self.sim_time_limit.to_string(), 
                self.trace_log_dir.clone()
            ])   
            .spawn();

        if spawn_result.is_err() {
            panic!("Error: Failed to start trace_recorder process: {}", spawn_result.err().unwrap());
        } else {
            self.recorder_process = Some(spawn_result.unwrap());
        }

        Ok(())

    }

    fn post_exec(&mut self, state: &mut S, _input: &I, _exit_kind: &ExitKind,) -> Result<(), Error> {

        // lambda function to add a state as a hashmap into the banquo Trace object
        fn create_state(x_val: f64, y_val: f64, z_val: f64) -> HashMap<&'static str, f64> {
            HashMap::from([
                ("x", x_val), 
                ("y", y_val), 
                ("z", z_val),
            ])
        } 

        println!("Hello from PhysicalObserver POST_exec!");

        if self.recorder_process.is_none() {
            // The recorder process should NOT be none...
            panic!("Error: Recorder process is none in post_exec!");
        }

        // Before reading the trace log, ensure the recorder process has finished
        let wait = self.recorder_process.as_mut().unwrap().wait().unwrap();
        if !wait.success() {
            // panic!("Error: Recorder process finished with error!");
            println!("Warning: Recorder process finished with error...");
        }
        self.recorder_process = None;

        let newest_trace_log_path = format!("{}/trace_{}.csv", self.trace_log_dir, state.executions());
        println!("Newest trace log path: {}", newest_trace_log_path);

        let csv_reader_status = csv::ReaderBuilder::new()
            .has_headers(false)
            .from_path(newest_trace_log_path);
        
        if csv_reader_status.is_err() {
            println!("Warning: Failed to open trace log file for execution {}", state.executions());
            println!("Skipping physical observation...");
            return Ok(());
        }

        let mut csv_reader = csv_reader_status.unwrap();

        let mut trace: Trace<HashMap<&str, f64>> = Trace::new();
        for result in csv_reader.deserialize() {
            let state: State = result.unwrap();
            trace.insert(state.time, create_state(state.x, state.y, state.z)); 
            // println!("Time: {}, x: {}, y: {}, z: {}", state.time, state.x, state.y, state.z);
        }

        // println!("Trace: {:?}", trace);

        // Finally, evaluate the trace against all STL formulas

        // So originally I asked chat to come up with mission boundaries for
        // the rover_rectangle.txt mission, but the mavlink map coordinates
        // do not line up with gazebo world coordinates. So I just watched the
        // mission a few times and the rover's pose throughout

        // let x_bounds = (-25, 10);
        // let y_bounds = (-5, 60);
        // Don't add z_bounds as its a very small bound and would hog the robustness reporting
        // let z_bounds = (0, 0.1);

        // Note: with banquo you can only use <= and constants in predicates
        let x_min_pred: banquo::Predicate = predicate!{ -25.0 <= x };
        let x_max_pred: banquo::Predicate = predicate!{ x <= 10.0 };
        let y_min_pred: banquo::Predicate = predicate!{ -5.0 <= y };
        let y_max_pred: banquo::Predicate = predicate!{ y <= 60.0 };
        // let z_min_pred: banquo::Predicate = predicate!{ -1.0 <= z };
        // let z_max_pred: banquo::Predicate = predicate!{ z <= 5.0 };

        // Either formulas are impossible to store generically in a single vector or I am a bot (most likely)
        // So for now just manually call evaluate() and push the results into latest_robustness_vec
        let formula_x = Always::unbounded(And::new(x_min_pred, x_max_pred));
        let formula_y = Always::unbounded(And::new(y_min_pred, y_max_pred));
        // let formula_z = Always::unbounded(And::new(z_min_pred, z_max_pred));

        self.latest_robustness_vec.clear();
        self.latest_robustness_vec.push(evaluate(&trace, &formula_x).unwrap());
        self.latest_robustness_vec.push(evaluate(&trace, &formula_y).unwrap());
        // self.latest_robustness_vec.push(evaluate(&trace, &formula_z).unwrap());

        // println!("Robustness for execution {}: {:?}", state.executions(), self.latest_robustness_vec);

        // Get minimum robustness value and place it in the state
        let mut min_robustness: f64 = f64::INFINITY;
        for robustness in self.latest_robustness_vec.iter() {
            if *robustness < min_robustness {
                min_robustness = *robustness;
            }
        }

        state.set_latest_robustness(min_robustness);

        // Also, log the robustness values to a file for later analysis
        let robustness_log_path = format!("{}/robustness_{}.csv", self.robustness_log_dir, state.executions());
        let log_dir = Path::new(&self.robustness_log_dir);
        create_dir_all(log_dir).expect("Unable to create robustness log directory");

        let mut file = OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&robustness_log_path)
            .expect("Unable to create robustness log file");

        writeln!(file, "Formula,Robustness").unwrap();
        writeln!(file, "X_boundary,{}", self.latest_robustness_vec[0]).unwrap();
        writeln!(file, "Y_boundary,{}", self.latest_robustness_vec[1]).unwrap();
        // writeln!(file, "Z_boundary,{}", self.latest_robustness_vec[2]).unwrap();
        file.flush().unwrap();

        Ok(())
    }
}