use scirs2_optimize::global::{ 
    BayesianOptimizationOptions,
    BayesianOptimizer,
    Space
};
use scirs2_optimize::prelude::Parameter;
use scirs2_core::ndarray::Array1;

pub struct Mutator {
    optimizer: BayesianOptimizer,
}

impl Mutator {

    pub fn new(space: Space, opt: BayesianOptimizationOptions) -> Self {
        Self {
            optimizer: BayesianOptimizer::new(space.clone(), Some(opt.clone()))
        }
    }

    pub fn ask(&mut self) -> Array1<f64> {
        self.optimizer.ask()
    }

    pub fn tell(&mut self, input: Array1<f64>, robustness: f64) {
        self.optimizer.tell(input, robustness);
    }

}