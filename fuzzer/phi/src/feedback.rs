use banquo::{EvaluationError, Formula, Trace, evaluate, predicate, trace};
use banquo::operators::{Always, And, Not, Implies};

use csv::Reader;
use serde::Deserialize;
use std::error::Error;

// Bless up ChatGPT for conjuring this type sorcery
// DynFormula allows us to store a single vector of STL formulas
type State = Vec<f64>;
type Metric = f64;
type FormulaError =
    banquo::operators::ForwardOperatorError<
        banquo::operators::BinaryOperatorError<
            banquo::predicate::FormulaError,
            banquo::predicate::FormulaError,
        >
    >;
pub type DynFormula = Box<dyn Formula<State, Metric = Metric, Error = FormulaError>>;

#[derive(Debug, Deserialize)]
struct Record {
    sim_time: f64,
    x_pos: f64,
    y_pos: f64,
    z_pos: f64,
}

pub struct Feedback {
    current_trace_num: usize, // Current trace number being evaluated
    last_result: bool, // Was the last input "interesting"? Did it lower a formula's robustness?
    metric_names: Vec<String>, // Names of the physical metrics being tracked
    stl_formula_vec: Vec<DynFormula>, // Vector of Banquo formulas to evaluate
    formula_robustness_vec: Vec<f64>, // Minimum robustness values observed for each formula
}

impl Feedback {

    pub fn new(trace_num: usize, metrics: Vec<String>, formulas: Vec<DynFormula>) -> Self {

        // Initialize robustness vector with +infinity values
        let robust_vec: Vec<f64> = vec![f64::INFINITY; formulas.len()];

        Self {
            current_trace_num: trace_num,
            last_result: false,
            metric_names: metrics,
            stl_formula_vec: formulas,
            formula_robustness_vec: robust_vec,
        }

    }

    pub fn last_result(&self) -> bool {
        self.last_result
    }

    pub fn is_interesting(&mut self) -> bool {

        // First, we must construct a Trace object from the CSV file
        let file_path: String = format!(
            "/home/ere/fire/FastDyn/fuzzer/phi/my_test_traces/trace_{}.csv", self.current_trace_num
        );

        let mut rdr: Reader<std::fs::File> = csv::ReaderBuilder::new()
            .has_headers(false)
            .from_path(file_path).unwrap();

        let mut trace: Trace<Vec<f64>> = Trace::new();
        for result in rdr.deserialize() {
            let record: Record = result.unwrap(); 
            trace.insert(record.sim_time, vec![record.x_pos, record.y_pos, record.z_pos]);
        }

        // The trace has been constructed. Now we can evaluate each formula and update
        // the robustness values.
        for (i, formula) in self.stl_formula_vec.iter().enumerate() {
            let result = evaluate(&trace, &formula).unwrap();
            println!("Formula {} robustness: {}", i, result);
        }

        self.last_result
    }

}