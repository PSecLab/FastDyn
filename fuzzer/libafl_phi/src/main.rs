mod phi_observer;
mod phi_feedback;
mod phi_objective;
mod cpexp_state;
mod phi_stage;
// mod cpexp_input;
mod new_input;
mod input_generator;
mod lambda_mutational;

#[cfg(windows)]
use std::ptr::write_volatile;
use std::{path::{self, PathBuf}, process::exit, ptr::write, thread::sleep, time::Duration};

use env_logger::Target;
use gz_msgs::param::{self, Param};
#[cfg(feature = "tui")]
use libafl::monitors::tui::TuiMonitor;
#[cfg(not(feature = "tui"))]
use libafl::monitors::SimpleMonitor;
use libafl::{
    corpus::{InMemoryCorpus, OnDiskCorpus}, events::SimpleEventManager, executors::{CommandExecutor, ExitKind, InProcessExecutor}, feedback_or, feedbacks::{CrashFeedback, MaxMapFeedback}, fuzzer::{Fuzzer, StdFuzzer}, generators::RandPrintablesGenerator, inputs::{BytesInput, HasTargetBytes}, mutators::{havoc_mutations::havoc_mutations, scheduled::HavocScheduledMutator}, observers::ConstMapObserver, schedulers::QueueScheduler, stages::mutational::StdMutationalStage, state::StdState
};
use libafl_bolts::{
    current_nanos, nonnull_raw_mut, nonzero, rands::StdRand, tuples::tuple_list, AsSlice,
};

use std::process::{Command, Child};

use scirs2_optimize::global::{ 
    BayesianOptimizationOptions,
    BayesianOptimizer,
    Space
};
use scirs2_optimize::prelude::Parameter;
use scirs2_core::ndarray::Array1;

use phi_observer::PhysicalObserver;
use phi_feedback::PhysicalFeedback;
use phi_objective::PhysicalObjective;
use cpexp_state::CPExpState;
use phi_stage::PhiStage;
use new_input::{CPExpInput, ParamInput, EnvInput, TargetInput};
use lambda_mutational::LambdaMutationalStage;

/// Coverage map with explicit assignments due to the lack of instrumentation
const SIGNALS_LEN: usize = 16;
static mut SIGNALS: [u8; SIGNALS_LEN] = [0; SIGNALS_LEN];
static mut SIGNALS_PTR: *mut u8 = &raw mut SIGNALS as _;

/// Assign a signal to the signals map
fn signals_set(idx: usize) {
    unsafe { write(SIGNALS_PTR.add(idx), 1) };
}

fn search_space_from_input_library(input_lib: &CPExpInput) -> Space {

    let mut space = Space::new();

    // Add parameter inputs to space
    let param_info_vec = input_lib.get_param_input();
    for param in param_info_vec.iter() {
        let param_name = param.get_name();
        let param_range = param.get_range();
        space = space.add(param_name, Parameter::Real(param_range.0, param_range.1));
    }

    // Add environmental inputs to space
    let env_info_vec = input_lib.get_env_input();
    for env in env_info_vec.iter() {
        let env_name = env.get_name();
        let env_range = env.get_range();
        space = space.add(env_name, Parameter::Real(env_range.0, env_range.1));
    }

    space
}

pub fn main() {
    env_logger::init();

    let mut harness = |input: &TargetInput| {

        println!("Hello from harness!");
        println!("Raw Ardu parameters: {:?}", input.get_param_bytes());
        println!("Converted Ardu parameters: {:?}", TargetInput::bytes_to_f32_vec(input.get_param_bytes()));
        println!("Input environment config: {:?}", input.get_env_config());
        
        // TODO: Run the simulation and apply inputs here!
        // For now, just start ./my_ackermann
        let ackermann_path = "/home/ere/fire/PRehost/gazebo/my_ackermann_w_state.sh";
        let spawn_gz = Command::new(ackermann_path).spawn();
        if spawn_gz.is_err() {
            panic!("Error: Failed to start my_ackermann process: {}", spawn_gz.err().unwrap());
        }

        // Right now we don't need to track the child but whateva
        // let mut gz_process = spawn_gz.unwrap();

        ExitKind::Ok
    };

    // Create an observation channel using the signals map
    let observer = unsafe { ConstMapObserver::from_mut_ptr("signals", nonnull_raw_mut!(SIGNALS)) };

    let physical_observer = PhysicalObserver::new(
        0.1,               // time step
        10.0,              // time limit
        "./trace_logs",  // directory to store the trace logs
    );

    // Feedback to rate the interestingness of an input
    let physical_feedback = PhysicalFeedback::new();
    let mut feedback = feedback_or!(physical_feedback, ); // MaxMapFeedback::new(&observer), );

    // A feedback to choose if an input is a solution or not
    let mut objective = feedback_or!(PhysicalObjective::new(), ); // CrashFeedback::new(),);

    // Build the optimizer and input library (CPExpInput) here
    // TODO: Load all this stuff from a file + create a function

    // --------------------------------
    // NOTE: Add Ardu parameters to space first, then env parameters!!
    // --------------------------------

    let mut param_info_vec: Vec<ParamInput> = Vec::new();

    param_info_vec.push(ParamInput::new_float(
        "stars",
        (0.0, 5.0),
        0.1,
    ));

    param_info_vec.push((ParamInput::new_categorical(
        "powers_of_ten",
        vec![1.0, 10.0, 100.0, 1000.0],

    )));

    let mut env_info_vec: Vec<EnvInput> = Vec::new();

    env_info_vec.push(EnvInput::new(
        "wind_speed",
        (0.0, 20.0),
        false,
    ));

    env_info_vec.push(EnvInput::new(
        "terrain_type",
        (0.0, 3.0), // 3 types: 0, 1, 2
        true,
    ));

    let input_library = CPExpInput::new(param_info_vec, env_info_vec);

    // println!("Input library: {:?}", input_library);

    let space = search_space_from_input_library(&input_library);

    // println!("Search space: {:?}", space);

    // Create options object
    let initial_points = 1;
    let mut opt = BayesianOptimizationOptions::default();
    opt.n_initial_points = initial_points.clone();

    let bo = BayesianOptimizer::new(space, Some(opt));

     let mut state = CPExpState::new(
        // RNG
        StdRand::with_seed(current_nanos()),
        // Corpus that will be evolved, we keep it in memory for performance
        InMemoryCorpus::new(),
        // Corpus in which we store solutions (crashes in this example),
        // on disk so the user can get them after stopping the fuzzer
        OnDiskCorpus::new(PathBuf::from("./crashes")).unwrap(),
        // States of the feedbacks.
        // The feedbacks can report the data that should persist in the State.
        &mut feedback,
        // Same for objective feedbacks
        &mut objective,
        // Bayesian Optimizer!
        Some(bo),
        // Start with phi stage first?
        false,
        // CPExpInput object for transforming inputs
        input_library,

    )
    .unwrap();

    // The Monitor trait define how the fuzzer stats are displayed to the user
    #[cfg(not(feature = "tui"))]
    let mon = SimpleMonitor::new(|s| println!("{s}"));
    #[cfg(feature = "tui")]
    let mon = TuiMonitor::builder()
        .title("Baby Fuzzer")
        .enhanced_graphics(false)
        .build();

    // The event manager handle the various events generated during the fuzzing loop
    // such as the notification of the addition of a new item to the corpus
    let mut mgr = SimpleEventManager::new(mon);

    // A queue policy to get testcasess from the corpus
    let scheduler = QueueScheduler::new();

    // A fuzzer with feedbacks and a corpus scheduler
    let mut fuzzer = StdFuzzer::new(scheduler, feedback, objective);

    // Create the executor for an in-process function with just one observer
    let mut executor = InProcessExecutor::new(
        &mut harness,
        tuple_list!(physical_observer, ), // observer, ),
        &mut fuzzer,
        &mut state,
        &mut mgr,
    )
    .expect("Failed to create the Executor");

    // Generate 8 initial inputs
    state 
        .generate_initial_inputs(
            &mut fuzzer, 
            &mut executor, 
            // &mut generator, 
            &mut mgr,
            initial_points,
        )
        .expect("Failed to generate the initial corpus");

    // panic!("Stopping after initial input generation for debugging purposes.");
    for _ in 0..3 {
        println!("Starting the fuzzing loop!");
    }

    let phi_stage = PhiStage::new(5);

    // Setup a mutational stage with a basic bytes mutator
    let mutator = HavocScheduledMutator::new(havoc_mutations());
    let mut stages = tuple_list!(phi_stage, LambdaMutationalStage::new(mutator),);

    fuzzer
        .fuzz_loop(&mut stages, &mut executor, &mut state, &mut mgr)
        .expect("Error in the fuzzing loop");
}
