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

use crate::cpexp_state::{HasLatestRobustness, HasOptimizer};

#[derive(Serialize, Deserialize, Debug)]
pub struct HardFaultObserver {
    name: Cow<'static, str>,
    faulting_pc: u32,
}

impl Named for HardFaultObserver {
    fn name(&self) -> &Cow<'static, str> {
        &self.name
    }
}

impl HardFaultObserver {

    pub fn new() -> Self {

        Self {
            name: Cow::from("HardFaultObserver"),
            faulting_pc: 0,
        }
    }

    pub fn get_faulting_pc(&self) -> u32 {
        self.faulting_pc
    }

}

impl<I, S> Observer<I, S> for HardFaultObserver 
where
    S: HasExecutions + HasOptimizer + HasLatestRobustness, 
{

    fn pre_exec(&mut self, state: &mut S, _input: &I) -> Result<(), Error> {

        println!("Hello from HardFaultObserver PRE_exec!");

        // // Nifty way to get the path of ./trace_recorder no matter where you run ./baby_fuzzer
        // let mut recorder_path = std::env::current_exe().expect("Failed to get current executable path");
        // recorder_path.pop(); // remove baby_fuzzer filename
        // recorder_path.push("trace_recorder");

        // let spawn_result = Command::new(recorder_path)
        //      .args([(state.executions() + 1).to_string(),
        //         self.trace_time_step.to_string(), 
        //         self.sim_time_limit.to_string(), 
        //         self.trace_log_dir.clone()
        //     ])   
        //     .spawn();

        // if spawn_result.is_err() {
        //     panic!("Error: Failed to start trace_recorder process: {}", spawn_result.err().unwrap());
        // } else {
        //     self.recorder_process = Some(spawn_result.unwrap());
        // }

        Ok(())

    }

    fn post_exec(&mut self, state: &mut S, _input: &I, _exit_kind: &ExitKind,) -> Result<(), Error> {

        // lambda to add a state as a hashmap into the banquo Trace object
        // fn create_state(x_val: f64, y_val: f64, z_val: f64) -> HashMap<&'static str, f64> {
        //     HashMap::from([
        //         ("x", x_val), 
        //         ("y", y_val), 
        //         ("z", z_val),
        //     ])
        // } 

        // println!("Hello from PhysicalObserver POST_exec!");

        // if self.recorder_process.is_none() {
        //     // The recorder process should NOT be none...
        //     panic!("Error: Recorder process is none in post_exec!");
        // }

        // // Before reading the trace log, ensure the recorder process has finished
        // let wait = self.recorder_process.as_mut().unwrap().wait().unwrap();
        // if !wait.success() {
        //     panic!("Error: Recorder process finished with error!");
        // }
        // self.recorder_process = None;

        // // Janky ass way to ensure everything is dead before starting again
        // // TODO find an elegant solution
        // // sleep(Duration::from_millis(3000));

        // // println!("All done!");

        // let newest_trace_log_path = format!("{}/trace_{}.csv", self.trace_log_dir, state.executions());
        // // println!("Newest trace log path: {}", newest_trace_log_path);

        // let mut csv_reader: Reader<std::fs::File> = csv::ReaderBuilder::new()
        //     .has_headers(false)
        //     .from_path(newest_trace_log_path).unwrap();

        // let mut trace: Trace<HashMap<&str, f64>> = Trace::new();
        // for result in csv_reader.deserialize() {
        //     let state: State = result.unwrap();
        //     trace.insert(state.time, create_state(state.x, state.y, state.z)); 
        //     // println!("Time: {}, x: {}, y: {}, z: {}", state.time, state.x, state.y, state.z);
        // }

        // // println!("Trace: {:?}", trace);

        // // Finally, evaluate the trace against all STL formulas
        // let x_pred: banquo::Predicate = predicate!{ x <= 110.0 };
        // let y_pred: banquo::Predicate = predicate!{ y <= 6.0 };
        // let z_pred: banquo::Predicate = predicate!{ z <= 0.16 };

        // // Either formulas are impossible to store generically in a single vector or I am a bot (most likely)
        // // So for now just manually call evaluate() and push the results into latest_robustness_vec
        // let formula: Always<And<banquo::Predicate, banquo::Predicate>> = Always::unbounded(And::new(x_pred, y_pred));
        // let formula2: Eventually<banquo::Predicate> = Eventually::unbounded(z_pred);

        // self.latest_robustness_vec.clear();
        // self.latest_robustness_vec.push(evaluate(&trace, &formula).unwrap());
        // self.latest_robustness_vec.push(evaluate(&trace, &formula2).unwrap());

        // // println!("Robustness for execution {}: {:?}", state.executions(), self.latest_robustness_vec);

        // // Get minimum robustness value and place it in the state
        // let mut min_robustness: f64 = f64::INFINITY;
        // for robustness in self.latest_robustness_vec.iter() {
        //     if *robustness < min_robustness {
        //         min_robustness = *robustness;
        //     }
        // }

        // state.set_latest_robustness(min_robustness);

        // // TODO: Write the robustness values to a separate log file

        Ok(())
    }
}