use libafl::executors::ExitKind;
use libafl::observers::Observer;
use libafl::state::HasExecutions;
use libafl::Error;
use libafl_bolts::Named;

use core::f64;
use csv::Reader;
use serde::{Deserialize, Serialize};
use std::borrow::Cow;
use std::collections::HashMap;
use std::fs::{File, OpenOptions, create_dir_all};
use std::io::{BufRead, BufReader, Write};
use std::path::Path;
use std::process::{Child, Command};
use std::thread::sleep;
use std::time::Duration;

use banquo_parser::{parse_formula, Formula, ParsedFormula, Trace};

use crate::cpexp_state::{HasLatestRobustness, HasOptimizer};

/// Path to the text file containing STL-style formulas, one per line.
/// Lines starting with `#` and blank lines are ignored.
const STL_FORMULAS_PATH: &str = "stl_formulas.txt";

// Example: Capturing timestamp, x, y, z coords
// #[derive(Deserialize)]
// struct State {
//     time: f64,
//     x: f64,
//     y: f64,
//     z: f64,
// }

#[derive(Deserialize)]
struct State {
    time: f64,
    roll: f64,
    pitch: f64,
    roll_rate: f64,
    lateral_accel: f64,
}

#[derive(Serialize, Deserialize)]
pub struct PhysicalObserver {
    name: Cow<'static, str>,
    trace_time_step: f64, // Time step between recorded trace points
    sim_time_limit: f64, // Maximum simulation time to record
    trace_log_dir: String, // Directory to store the trace logs
    robustness_log_dir: String, // Directory to store the robustness logs
    latest_robustness_vec: Vec<f64>, // Vector to store latest robustness values for each STL formula

    /// Parsed STL-style formulas loaded from `stl_formulas.txt` at startup.
    #[serde(skip)]
    formulas: Vec<ParsedFormula>,

    // The indices of the formulas to be optimized in this instantiation.
    // We observe all formulas, but optimize a subset.
    #[serde(skip)]
    optimized_formula_indices: Vec<usize>,

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

        // Load STL formulas from the configured text file.
        let (formulas, opt_indices) = Self::load_formulas_from_file(STL_FORMULAS_PATH);

        // Initialize robustness vector with +inf for each loaded formula so that
        // the first evaluation will always improve it.
        let robustness_vec: Vec<f64> = if formulas.is_empty() {
            // Fail fast with a clear message if no formulas were loaded.
            panic!(
                "No STL formulas loaded from '{}'. \
                 Please create this file with one STL-style formula per line.",
                STL_FORMULAS_PATH
            );
        } else {
            vec![f64::INFINITY; formulas.len()]
        };

        Self {
            name: Cow::from("PhysicalObserver"),
            trace_time_step: step,
            sim_time_limit: limit,
            trace_log_dir: String::from(trace_log_dir),
            robustness_log_dir: String::from(robustness_log_dir),
            latest_robustness_vec: robustness_vec,
            formulas,
            optimized_formula_indices: opt_indices,
            recorder_process: None,
        }
    }

    pub fn get_robustness_vec(&self) -> &Vec<f64> {
        &self.latest_robustness_vec
    }

    /// Load STL-style formulas from a plain text file using `banquo-parser`.
    ///
    /// - One formula per non-empty line.
    /// - Lines starting with `#` are treated as comments and ignored.
    fn load_formulas_from_file(path: &str) -> (Vec<ParsedFormula>, Vec<usize>) {
        let file = File::open(path).unwrap_or_else(|err| {
            panic!(
                "Failed to open STL formula file '{}': {}\n\
                 Create this file and add one formula per line, \
                 e.g.:\n  always -0.78539816339 <= roll and roll <= 0.78539816339",
                path, err
            )
        });

        let reader = BufReader::new(file);
        let mut formulas = Vec::new();
        let mut opt_indices: Vec<usize> = Vec::new();

        for (idx, line_res) in reader.lines().enumerate() {
            let line_num = idx + 1;
            let line = line_res.unwrap_or_else(|err| {
                panic!("Error reading line {} from '{}': {}", line_num, path, err)
            });

            // Work with an owned string for the formula to avoid lifetime
            // issues tied to the `line` binding.
            let formula_str = line.trim().to_owned();

            // Skip blank lines. 
            if formula_str.is_empty() {
                continue;
            }

            if formula_str.starts_with('#') {
                // Check for optimization flag in comment lines, e.g.:
                // # OPTIMIZE
                if formula_str == "# OPTIMIZE" {
                    opt_indices.push(formulas.len());
                }

                continue;
            }

            let parsed = parse_formula(&formula_str).unwrap_or_else(|e| {
                panic!(
                    "Failed to parse STL formula on line {} of '{}': \"{}\"\nError: {}",
                    line_num, path, formula_str, e
                );
            });

            formulas.push(parsed);
        }

        if opt_indices.is_empty() {
            for i in 0..formulas.len() {
                opt_indices.push(i);
            }
        }

        (formulas, opt_indices)
    }

}

impl<I, S> Observer<I, S> for PhysicalObserver 
where
    S: HasExecutions + HasOptimizer + HasLatestRobustness, 
{

    fn pre_exec(&mut self, state: &mut S, _input: &I) -> Result<(), Error> {

        // println!("Hello from PhysicalObserver PRE_exec!");
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
        // fn create_state(x_val: f64, y_val: f64, z_val: f64) -> HashMap<&'static str, f64> {
        //     HashMap::from([
        //         ("x", x_val), 
        //         ("y", y_val), 
        //         ("z", z_val),
        //     ])
        // } 

        fn create_state(
            roll: f64,
            pitch: f64,
            roll_rate: f64,
            lateral_accel: f64,
        ) -> HashMap<String, f64> {
            HashMap::from([
                ("roll".to_string(), roll),
                ("pitch".to_string(), pitch),
                ("roll_rate".to_string(), roll_rate),
                ("lateral_accel".to_string(), lateral_accel),
            ])
        }

        // println!("Hello from PhysicalObserver POST_exec!");

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
        // println!("Newest trace log path: {}", newest_trace_log_path);

        let csv_reader_status = csv::ReaderBuilder::new()
            .has_headers(false)
            .from_path(newest_trace_log_path);
        
        if csv_reader_status.is_err() {
            println!("Warning: Failed to open trace log file for execution {}", state.executions());
            println!("Skipping physical observation...");
            return Ok(());
        }

        let mut csv_reader = csv_reader_status.unwrap();

        let mut trace: Trace<HashMap<String, f64>> = Trace::new();
        for result in csv_reader.deserialize() {
            let state: State = result.unwrap();
            trace.insert(state.time, create_state(state.roll, state.pitch, state.roll_rate, state.lateral_accel));
            // trace.insert(state.time, create_state(state.x, state.y, state.z)); 
            // println!("Time: {}, x: {}, y: {}, z: {}", state.time, state.x, state.y, state.z);
        }

        // println!("Trace: {:?}", trace);

        // Finally, evaluate the trace against all STL formulas that were
        // parsed from the external file using `banquo-parser`.

        self.latest_robustness_vec.clear();

        for (idx, formula) in self.formulas.iter().enumerate() {
            let metrics = formula
                .evaluate(&trace)
                .unwrap_or_else(|e| {
                    panic!(
                        "Error evaluating STL formula #{} from '{}': {}",
                        idx + 1,
                        STL_FORMULAS_PATH,
                        e
                    )
                });

            // Reduce the per-time-step metrics to a single robustness value by
            // taking the minimum over the trace, matching the classic STL
            // robustness semantics.
            let robustness = metrics
                .iter()
                .fold(f64::INFINITY, |acc, (t, v)| {
                    let _ = t; // ignore time, only robustness value matters
                    if *v < acc { *v } else { acc }
                });

            self.latest_robustness_vec.push(robustness);
        }

        // println!("Robustness for execution {}: {:?}", state.executions(), self.latest_robustness_vec);

        // Get minimum robustness value of the OPTIMIZED formulas, and put it in the state to tell the optimizer
        let mut min_optimized_robustness: f64 = f64::INFINITY;
        for opt in self.optimized_formula_indices.iter() {
            let current_robustness = self.latest_robustness_vec[*opt];
            if current_robustness < min_optimized_robustness {
                min_optimized_robustness = current_robustness;
            }
        }
        state.set_latest_robustness(min_optimized_robustness);

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
        for (i, robustness) in self.latest_robustness_vec.iter().enumerate() {
            writeln!(file, "Formula_{},{}", i, robustness).unwrap();
        }
        file.flush().unwrap();

        Ok(())
    }
}