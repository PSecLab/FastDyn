mod phi_observer;
mod phi_feedback;
mod phi_objective;
mod cpexp_state;
mod phi_stage;
mod cpexp_input;
mod lambda_mutational;
mod mission_execution;

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

use std::process::{Command, Child, Stdio};

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
use cpexp_state::{CPExpState, HasInputLibrary};
use phi_stage::PhiStage;
use cpexp_input::{CPExpInput, ParamInput, EnvInput, TargetInput, HasParamBytes, HasEnvConfig};
use lambda_mutational::LambdaMutationalStage;

use mission_execution::execute_mission;

use libafl::executors::{Executor, WithObservers};
use std::marker::PhantomData;
use libafl::state::{HasCorpus, HasExecutions};

// -------------------------------
// CONSTANT VALUES
const MISSION_TIMEOUT: f64 = 420.0; // seconds == 7 minutes for rover_rectangle.txt
const RECORDING_TIMESTEP: f64 = 0.5; // seconds
const TRACE_LOG_PATH: &str = "./trace_logs";
const ROBUSTNESS_LOG_PATH: &str = "./robustness_logs";
const CRASH_LOG_PATH: &str = "./crashes";
// -------------------------------

/*
    Once the CPExpInput is built, you can use this helper function to automatically
    create a search space object for the optimizer.
*/
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

struct FastDynExecutor<S> {
    phantom: PhantomData<S>,
    // fuzz_buffer: *mut FuzzBuffer,
}

impl<S> FastDynExecutor<S> {
    // pub fn new(_state: &S, fuzz_buffer: *mut FuzzBuffer) -> Self {
    pub fn new(_state: &S,) -> Self {
        Self {
            phantom: PhantomData,
            // fuzz_buffer,
        }
    }
}

impl<EM, S, Z> Executor<EM, TargetInput, S, Z> for FastDynExecutor<S>
where
    S: HasCorpus<TargetInput> + HasExecutions + HasInputLibrary,
    // I: TargetInput// HasParamBytes + HasEnvConfig,
{
    fn run_target(
        &mut self,
        _fuzzer: &mut Z,
        state: &mut S,
        _mgr: &mut EM,
        input: &TargetInput,
    ) -> Result<ExitKind, libafl::Error> {

        let executions = state.executions_mut();
        *executions += 1;

        let mission_timeout: f64 = MISSION_TIMEOUT; // seconds

        let param_info = state.input_library().get_param_input().clone();
        let mut param_names = String::new();
        for param in param_info.iter() {
            param_names.push_str(&param.get_name());
            param_names.push(',');
        }

        Ok(execute_mission(input, param_names, mission_timeout))
    }
}

pub fn main() {
    env_logger::init();

    // INPUT YOUR PATHS HERE
    let run_services_path = "/root/fire/fuzz_testing/FastDyn/courbet/gazebo/"; // run_and_attach_services.sh
    let qemu_build_path = "/root/fire/fuzz_testing/qemu/build"; // ../fd_rover.sh
    let mav_c2_path = "/root/fire/fuzz_testing/FastDyn/courbet/mavlink/"; // mav_command_and_control.py

    let physical_observer = PhysicalObserver::new(
        RECORDING_TIMESTEP, // time step
        MISSION_TIMEOUT, // mission time limit
        TRACE_LOG_PATH, // directory to store the trace logs
        ROBUSTNESS_LOG_PATH, // directory to store the robustness logs
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

    // https://ardupilot.org/rover/docs/parameters.html

    let mut param_info_vec: Vec<ParamInput> = Vec::new();

    param_info_vec.push(ParamInput::new_float(
        "TELEM_DELAY",
        (0.0, 30.0), // min, max
        1.0, // increment
    ));

    param_info_vec.push(ParamInput::new_float(
        "FS_OPTIONS",
        (0.0, 100.0), // min, max
        0.0, // increment
    ));

    // Example of categorical parameter input
    // param_info_vec.push((ParamInput::new_categorical(
    //     "powers_of_ten",
    //     vec![1.0, 10.0, 100.0, 1000.0], // categories
    // )));

    let mut env_info_vec: Vec<EnvInput> = Vec::new();

    env_info_vec.push(EnvInput::new(
        "wind_speed",
        (0.0, 20.0), // min, max
        false, // truncate = false --> float
    ));

    env_info_vec.push(EnvInput::new(
        "terrain_type",
        (0.0, 3.0), // 3 types: 0, 1, 2
        true, // truncate = true --> integer
    ));

    let input_library = CPExpInput::new(param_info_vec, env_info_vec);

    // println!("Input library: {:?}", input_library);

    let space = search_space_from_input_library(&input_library);

    // println!("Search space: {:?}", space);

    // Create options object
    let initial_points = 2; // This controls how many initial inputs are generated before fuzzing starts
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
        OnDiskCorpus::new(PathBuf::from(CRASH_LOG_PATH)).unwrap(),
        // States of the feedbacks.
        // The feedbacks can report the data that should persist in the State.
        &mut feedback,
        // Same for objective feedbacks
        &mut objective,
        // Bayesian Optimizer!
        Some(bo),
        // Start with phi stage first?
        false,
        // CPExpInput object for transforming TargetInputs (serializable) into usable values
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

    // let executor = FastDynExecutor::new(&state, fuzz_buffer);
    let executor = FastDynExecutor::new(&state);
    let mut executor = WithObservers::new(executor, tuple_list!(physical_observer, )); // cvg_observer, ));

    // Generate initial inputs using the optimizer
    state 
        .generate_initial_inputs(
            &mut fuzzer, 
            &mut executor, 
            &mut mgr,
            initial_points,
        )
        .expect("Failed to generate the initial corpus");

    for _ in 0..3 {
        println!("Starting the fuzzing loop!");
    }

    // Setup a mutational stage with a basic bytes mutator
    let mutator = HavocScheduledMutator::new(havoc_mutations());

    // TODO: @mike - Add calibration and stats stages once you put in your lambda feedback
    let mut stages = tuple_list!(
        // calibration_stage,
        PhiStage::new(5), 
        LambdaMutationalStage::new(mutator),
        // stats_stage,
    );

    fuzzer
        .fuzz_loop(&mut stages, &mut executor, &mut state, &mut mgr)
        .expect("Error in the fuzzing loop");
}
