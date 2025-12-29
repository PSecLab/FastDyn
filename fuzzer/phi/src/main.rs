use scirs2_optimize::global::{ 
    BayesianOptimizationOptions,
    BayesianOptimizer,
    Space
};
use scirs2_optimize::prelude::Parameter;

// Want the optimizer to converge towards this value
const OPT_TARGET: f64 = 0.25;
const INITIAL_POINTS: usize = 100;
const LOWER_BOUND: f64 = -1.0;
const UPPER_BOUND: f64 = 1.0;

fn my_ask_and_tell(bo: &mut BayesianOptimizer, iterations: usize) {
    for i in 0..iterations {
        let input = bo.ask();
        let num_value = input[0]; //.clamp(LOWER_BOUND, UPPER_BOUND);
        let robustness = f64::abs(OPT_TARGET - num_value);
        println!("ITERATION {i}:");
        println!("\tGot from optimizer: {num_value}");
        println!("\tSimulated Robustness: {robustness}");
        bo.tell(input, robustness); 
    }
}

fn main() {
    
    println!("EXPERIMENT DETAILS:");
    println!("\tTarget input: {OPT_TARGET}");
    println!("\tSearch bounds: ({LOWER_BOUND}, {UPPER_BOUND})");
    println!("\tNumber of points to explore before optimization: {INITIAL_POINTS}");

    // Define search space
    let space = Space::new().add("Burger", Parameter::Real(LOWER_BOUND, UPPER_BOUND));

    // Create optimizer options with more evaluations
    let mut options = BayesianOptimizationOptions::default();
    options.n_initial_points = INITIAL_POINTS;

    // Create optimizer
    let mut bo = BayesianOptimizer::new(space, Some(options));

    println!("---------------------------------------------------");
    println!("TRAINING OPTIMIZER WITH {INITIAL_POINTS} INITIAL POINTS...");
    my_ask_and_tell(&mut bo, INITIAL_POINTS);
    println!("---------------------------------------------------");
    println!("What did the optimizer learn??");
    println!("---------------------------------------------------");
    my_ask_and_tell(&mut bo, 10);

} 
