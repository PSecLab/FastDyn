use libafl::stages::Stage;
use libafl::stages::Restartable;
use libafl::state::HasCurrentTestcase;
use libafl::state::HasExecutions;
use std::{borrow::Cow, num::NonZeroUsize};
use libafl_bolts::Named;
use crate::cpexp_input::{EnvInput, ParamInput, CPExpInput};
use crate::cpexp_state::HasOptimizeParams;

pub struct PhiStage {
    name: Cow<'static, str>,
    // The number of executions performed during this current stage
    current_executions: usize,
    max_executions: usize,
    total_executions: usize,
}

impl Named for PhiStage {
    fn name(&self) -> &Cow<'static, str> {
        &self.name
    }
}

impl<E, EM, S, Z> Stage<E, EM, S, Z> for PhiStage 
where 
    S: HasOptimizeParams + HasCurrentTestcase<CPExpInput>,
    Z: libafl::fuzzer::Evaluator<E, EM, crate::cpexp_input::CPExpInput, S>,
{

    fn perform(
            &mut self,
            fuzzer: &mut Z,
            executor: &mut E,
            state: &mut S,
            manager: &mut EM,
        ) -> Result<(), libafl::Error> {
        
        println!("Hello from PhiStage perform()!");

        // TODO: Genearate input, pass it to executor

        let env_input = EnvInput::new("I AM FROM PHISTAGE", (-10.0, 10.0), false);
        let param_input = ParamInput::new_float("I AM ALSO FROM PHISTAGE", (-5.0, 5.0), 0.1);
        let input = CPExpInput::new(vec![param_input], vec![env_input]);

        fuzzer.evaluate_input(state, executor, manager, &input)?;


        self.current_executions += 1;
        self.total_executions += 1;
        Ok(())

    }
}

impl<S> Restartable<S> for PhiStage 
where
    S: HasOptimizeParams,
{

    // Called after perform()
    fn clear_progress(&mut self, state: &mut S) -> Result<(), libafl::Error> {

        println!("Hello from PhiStage clear_progress()!");

        // Idk what I would do here yet

        Ok(())
    }

    // Called before perform()
    fn should_restart(&mut self, state: &mut S) -> Result<bool, libafl::Error> {

        println!("Hello from PhiStage should_restart()! Current optimizer executions: {}", self.current_executions);
        println!("Hello from PhiStage should_restart()! Total optimizer executions: {}", self.total_executions);

        // The phi stage runs for max_executions, then switches to lambda stage
        // We rely on lambda stage to yield (set optimize_params to true)
        // if self.current_executions > self.max_executions {
        //     self.current_executions = 0;
        //     *state.optimize_params_mut() = false;
        // } 

        Ok(state.optimize_params())

    }

}

impl PhiStage {

    pub fn new(executions: usize) -> Self {
        Self {
            name: Cow::from("PhiStage"),
            current_executions: 0,
            max_executions: executions,
            total_executions: 0,
        }
    }

}