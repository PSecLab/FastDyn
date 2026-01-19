mod phi_observer;
mod phi_feedback;
mod phi_objective;
mod cpexp_state;
mod phi_stage;
// mod cpexp_input;
mod new_input;
mod input_generator;

#[cfg(windows)]
use std::ptr::write_volatile;
use std::{path::PathBuf, ptr::write, thread::sleep, time::Duration};

use env_logger::Target;
use gz_msgs::param;
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

/// Coverage map with explicit assignments due to the lack of instrumentation
const SIGNALS_LEN: usize = 16;
static mut SIGNALS: [u8; SIGNALS_LEN] = [0; SIGNALS_LEN];
static mut SIGNALS_PTR: *mut u8 = &raw mut SIGNALS as _;

/// Assign a signal to the signals map
fn signals_set(idx: usize) {
    unsafe { write(SIGNALS_PTR.add(idx), 1) };
}

pub fn main() {
    env_logger::init();
    // The closure that we want to fuzz
    // let mut harness = |input: &BytesInput| {
    //     let target = input.target_bytes();
    //     let buf = target.as_slice();
    //     signals_set(0);
    //     if !buf.is_empty() && buf[0] == b'a' {
    //         signals_set(1);
    //         if buf.len() > 1 && buf[1] == b'b' {
    //             signals_set(2);
    //             if buf.len() > 2 && buf[2] == b'c' {
    //                 #[cfg(unix)]
    //                 panic!("Artificial bug triggered =)");

    //                 // panic!() raises a STATUS_STACK_BUFFER_OVERRUN exception which cannot be caught by the exception handler.
    //                 // Here we make it raise STATUS_ACCESS_VIOLATION instead.
    //                 // Extending the windows exception handler is a TODO. Maybe we can refer to what winafl code does.
    //                 // https://github.com/googleprojectzero/winafl/blob/ea5f6b85572980bb2cf636910f622f36906940aa/winafl.c#L728
    //                 #[cfg(windows)]
    //                 unsafe {
    //                     // Replace zero-ptr with the below function, suggested by Clippy
    //                     write_volatile(std::ptr::null_mut::<u32>(), 0);
    //                 }
    //             }
    //         }
    //     }
    //     ExitKind::Ok
    // };

    let mut harness = |input: &TargetInput| {

        println!("Hello from harness!");
        println!("Input parameters: {:?}", input.get_param_bytes());
        println!("Input environment config: {:?}", input.get_env_config());
        
        // TODO: Run the simulation and apply inputs here!

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

    // create a State from scratch
    // let mut state = StdState::new(
    //     // RNG
    //     StdRand::with_seed(current_nanos()),
    //     // Corpus that will be evolved, we keep it in memory for performance
    //     InMemoryCorpus::new(),
    //     // Corpus in which we store solutions (crashes in this example),
    //     // on disk so the user can get them after stopping the fuzzer
    //     OnDiskCorpus::new(PathBuf::from("./crashes")).unwrap(),
    //     // States of the feedbacks.
    //     // The feedbacks can report the data that should persist in the State.
    //     &mut feedback,
    //     // Same for objective feedbacks
    //     &mut objective,
    // )
    // .unwrap();

    // Define search space
    let mut space = Space::new();
    // space = space.add("categorical_param", Parameter::Categorical(vec!["A".to_string(), "B".to_string(), "C".to_string()]));
    space = space.add("throttle", Parameter::Real(-1.0, 1.0));
    space = space.add("wind_speed", Parameter::Real(0.0, 20.0));

    // Create options object
    let mut opt = BayesianOptimizationOptions::default();
    opt.n_initial_points = 100;

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
        true,
        CPExpInput::new(Vec::<ParamInput>::new(), Vec::<EnvInput>::new()),

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

    // Generator of printable bytearrays of max size 32
    let mut generator = RandPrintablesGenerator::new(nonzero!(32));

    // let param_val: f32 = 13.07;
    // let value_bytes = param_val.to_le_bytes();
    // let param_bytes = BytesInput::new(value_bytes.to_vec());

    // let env_config = String::from("Wind:5.75343463,");
    // let input = TargetInput::new(param_bytes, env_config);

    // state
    //     .add_initial_cpexp_inputs(&mut fuzzer, &mut executor, &mut mgr, &mut input)
    //     .expect("Failed to generate the initial corpus");

    // Generate 8 initial inputs
    state 
        .generate_initial_inputs(
            &mut fuzzer, 
            &mut executor, 
            // &mut generator, 
            &mut mgr, 
            8,
        )
        .expect("Failed to generate the initial corpus");

    // Setup a mutational stage with a basic bytes mutator
    // let mutator = HavocScheduledMutator::new(havoc_mutations());

    let phi_stage = PhiStage::new(20);

    let mut stages = tuple_list!(phi_stage,); // StdMutationalStage::new(mutator),);

    fuzzer
        .fuzz_loop(&mut stages, &mut executor, &mut state, &mut mgr)
        .expect("Error in the fuzzing loop");
}
