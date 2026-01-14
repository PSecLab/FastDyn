use libafl::feedbacks::{Feedback, StateInitializer};
use libafl_bolts::Named;
use libafl::Error;


use banquo::{EvaluationError, Formula, Trace, evaluate, predicate, trace};
use banquo::operators::{Always, And, Not, Implies};
use std::borrow::Cow;


pub struct PhysicalFeedback {
    name: Cow<'static, str>,
    current_trace_num: usize, // Current trace number being evaluated
    // last_result: bool, // Was the last input "interesting"? Did it lower a formula's robustness?
    metric_names: Vec<String>, // Names of the physical metrics being tracked
    // stl_formula_vec: Vec<DynFormula>, // Vector of Banquo formulas to evaluate
    formula_robustness_vec: Vec<f64>, // Minimum robustness values observed for each formula
}

impl Named for PhysicalFeedback {
    fn name(&self) -> &Cow<'static, str> {
        &self.name
    }
}

impl<S> StateInitializer<S> for PhysicalFeedback {
    fn init_state(&mut self, _state: &mut S) -> Result<(), libafl::Error> {
        Ok(())
    }
}

impl PhysicalFeedback {

    // pub fn new(trace_num: usize, metrics: Vec<String>, formulas: Vec<DynFormula>) -> Self {
    pub fn new(trace_num: usize, metrics: Vec<String>, formula_count: usize) -> Self {

        // Initialize robustness vector with +infinity values
        let robust_vec: Vec<f64> = vec![f64::INFINITY; formula_count]; //formulas.len()];

        Self {
            name: Cow::from("PhysicalFeedback"),
            current_trace_num: trace_num,
            // last_result: false,
            metric_names: metrics,
            // stl_formula_vec: formulas,
            formula_robustness_vec: robust_vec,
        }

    }

}

impl<EM, I, OT, S> Feedback<EM, I, OT, S> for PhysicalFeedback {
    fn is_interesting(
            &mut self,
            _state: &mut S,
            _manager: &mut EM,
            _input: &I,
            _observers: &OT,
            _exit_kind: &libafl::executors::ExitKind,
        ) -> Result<bool, Error> {

        println!("Hello from PhysicalFeedback is_interesting! Current trace num = {}", self.current_trace_num);
        self.current_trace_num += 1;
        
        // if (intersting) {
        //     Ok(true)
        // } else {
        //     Ok(false)
        // }
        
        // todo!()
        Ok(false)
    }
}