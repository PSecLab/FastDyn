// LibAFL style modules
mod executor;
mod observer;
mod mutator;
mod feedback;

use mutator::Mutator;

use scirs2_optimize::global::{ 
    BayesianOptimizationOptions,
    Space
};
use scirs2_optimize::prelude::Parameter;
use scirs2_core::ndarray::Array1;

fn main() {

    // Define search space
    let mut space = Space::new();
    // space = space.add("categorical_param", Parameter::Categorical(vec!["A".to_string(), "B".to_string(), "C".to_string()]));
    space = space.add("throttle", Parameter::Real(-1.0, 1.0));
    space = space.add("wind_speed", Parameter::Real(0.0, 20.0));

    // Create options object
    let mut opt = BayesianOptimizationOptions::default();
    opt.n_initial_points = 100;

    let mut mutator = Mutator::new(space, opt);

    // initial points phase
    for _ in 0..100 {
        let input: Array1<f64> = mutator.ask();
        println!("Got from optimizer: {}", input);
        let robustness: f64 = 0.0; // Placeholder for actual robustness computation
        mutator.tell(input, robustness);
    }

    println!("--- Starting exploitation phase ---");

    // exploitation phase
    // Important: 
    for _ in 0..50 {
        let input: Array1<f64> = mutator.ask();
        println!("Got from optimizer: {}", input);
        let robustness: f64 = 0.0; // Placeholder for actual robustness computation
        mutator.tell(input, robustness);
    }
}