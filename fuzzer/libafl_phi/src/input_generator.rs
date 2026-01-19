use std::num::NonZeroUsize;
use std::cmp::max;
use libafl::inputs::BytesInput;
use libafl::generators::Generator;
use libafl::state::HasRand;
use libafl::Error;
use libafl_bolts::rands::Rand;
use libafl_bolts::nonzero;

use crate::cpexp_state::HasOptimizer;
use crate::new_input::TargetInput;
use crate::new_input::CPExpInput;

#[derive(Debug, Clone)]
pub struct TargetInputGenerator {
    input_library: CPExpInput,
}

impl<S> Generator<TargetInput, S> for TargetInputGenerator
where
    S: HasRand + HasOptimizer,
{
    fn generate(&mut self, state: &mut S) -> Result<TargetInput, Error> {

        let min_size = 1;
        // Each ardu param is an f32 (4 bytes)
        let max_size = self.input_library.get_param_input().len() * 4;

        let mut size = state
            .rand_mut()
            .between(min_size, max_size);
        size = max(size, 1);
        let random_bytes: Vec<u8> = (0..size)
            .map(|_| state.rand_mut().below(nonzero!(256)) as u8)
            .collect();

        // Ask the optimizer for an environment configuration
        let environment_values = state.ask_optimizer();
        
        // Actually, we could just generate initial inputs using the optimizer
        // Then the parameter values would for sure be valid
        // So for now, I will leave this unfinished but maybe I'll come back to it
        todo!();

        //Ok(TargetInput::new(random_bytes))
    }
}

impl TargetInputGenerator {
    pub fn new(input_library: CPExpInput) -> Self {
        Self {
            input_library,
        }
    }
}