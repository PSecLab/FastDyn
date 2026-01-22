#[cfg(windows)]
use std::ptr::write_volatile;
use std::{marker::PhantomData, path::PathBuf, ptr::write};
use memmap2::MmapMut;
use std::fs::OpenOptions;
use std::path::Path;

#[cfg(feature = "tui")]
use libafl::monitors::tui::TuiMonitor;
#[cfg(not(feature = "tui"))]
use libafl::monitors::SimpleMonitor;
use libafl::{
    corpus::{InMemoryCorpus, OnDiskCorpus},
    events::SimpleEventManager,
    executors::{Executor, ExitKind, WithObservers},
    feedback_and_fast,
    feedbacks::{CrashFeedback, MaxMapFeedback},
    fuzzer::{Fuzzer, StdFuzzer},
    generators::RandPrintablesGenerator,
    inputs::{BytesInput, HasTargetBytes, Input, InputConverter},
    mutators::{havoc_mutations::havoc_mutations, scheduled::HavocScheduledMutator},
    observers::StdMapObserver,
    schedulers::QueueScheduler,
    stages::{mutational::StdMutationalStage, AflStatsStage, CalibrationStage},
    state::{HasCorpus, HasExecutions, StdState},
    BloomInputFilter, StdFuzzerBuilder,
};
use libafl_bolts::{
    current_nanos,
    nonzero,
    rands::StdRand,
    tuples::tuple_list,
    AsSlice,
};
use core::num::NonZeroUsize;
use std::fs::File;
use std::collections::{HashMap, HashSet};
use std::io::{self, Read, Write, BufReader};
use std::thread::JoinHandle;
use std::ffi::CStr;
use std::os::raw::c_char;
use std::sync::{
    mpsc::{Sender, Receiver, channel},
    atomic::{AtomicU32, Ordering},
};
use std::sync::{RwLock, Mutex, Arc};
use std::ptr;
use lazy_static::lazy_static;

/// Coverage map with explicit assignments due to the lack of instrumentation
static mut SIGNALS: [u8; 16] = [0; 16];
static mut SIGNALS_PTR: *mut u8 = &raw mut SIGNALS as _;
// TODO: This will break soon, fix me! See https://github.com/AFLplusplus/LibAFL/issues/2786
#[allow(static_mut_refs)] // only a problem in nightly
static SIGNALS_LEN: usize = unsafe { SIGNALS.len() };

/// Assign a signal to the signals map
fn signals_set(idx: usize) {
    unsafe { write(SIGNALS_PTR.add(idx), 1) };
}

//This will hold the coverage from the run.
const MAP_SIZE: usize = 65536; // same as AFL, make sure the definition of this size in C is the same
#[no_mangle]
pub static mut CVG: [u8; MAP_SIZE] = [0; MAP_SIZE];

#[repr(C)]
pub struct FuzzInput {
    len: usize,
    data: *mut u8,
}

#[repr(C)]
pub struct FuzzBuffer {
    status: std::sync::atomic::AtomicU32,
    assert: std::sync::atomic::AtomicU32,
    buffer: *mut *mut FuzzInput,
}

extern "C" {
    fn fuzz_buffer_create(anchor_id: u32) -> *mut FuzzBuffer;
    //fn fuzz_buffer_destroy(fb: *mut FuzzBuffer); // This should never really need to be removed
    fn fuzz_buffer_write(fb: *mut FuzzBuffer, input: *mut FuzzInput) -> bool;
    fn fuzz_check_empty(fb: *mut FuzzBuffer) -> bool;
    fn fuzz_check_assert(fb: *mut FuzzBuffer) -> u32;
}

struct FastDynExecutor<S> {
    phantom: PhantomData<S>,
    fuzz_buffer: *mut FuzzBuffer,
}

impl<S> FastDynExecutor<S> {
    pub fn new(_state: &S, fuzz_buffer: *mut FuzzBuffer) -> Self {
        Self {
            phantom: PhantomData,
            fuzz_buffer,
        }
    }
}

impl<EM, I, S, Z> Executor<EM, I, S, Z> for FastDynExecutor<S>
where
    S: HasCorpus<I> + HasExecutions,
    I: HasTargetBytes,
{
    fn run_target(
        &mut self,
        _fuzzer: &mut Z,
        state: &mut S,
        _mgr: &mut EM,
        input: &I,
    ) -> Result<ExitKind, libafl::Error> {
        // We need to keep track of the exec count.
        *state.executions_mut() += 1;

        let target = input.target_bytes();
        let buf = target.as_slice();

        let mut vec = buf.to_vec().into_boxed_slice();
        let input = Box::new(FuzzInput {
            len: vec.len(),
            data: vec.as_mut_ptr(),
        });

        Box::leak(vec);

        let input_ptr = Box::into_raw(input);
        
        loop {
            let pushed = unsafe { fuzz_buffer_write(self.fuzz_buffer, input_ptr) };
            if pushed {
                break;
            }

            // Give the consumer time to read
            std::hint::spin_loop();
        }

        // if fuzzing target doesn't finish quickly, move to a slower less cpu-intensive option
        const SPIN_ITERS: usize = 1_000;
        const YIELD_ITERS: usize = 10_000;
        let mut spins = 0;
        loop {
            let finished = unsafe { fuzz_check_empty(self.fuzz_buffer) };
            if finished {
                break;
            }

            // Give firmware time to reach next anchor
            if spins < SPIN_ITERS {
                std::hint::spin_loop();
            } else if spins < YIELD_ITERS {
                std::thread::yield_now();
            } else {
                std::thread::sleep(std::time::Duration::from_micros(500));
            }
        }

	    // Build transitions
		let mut crash = false;
        unsafe {
            let value = fuzz_check_assert(self.fuzz_buffer); 
            if value != 0 {
                crash = true;
            }
        }

	    //println!("Unique nodes: {}", edges.len());

		/* DEBUG COnditional  
    	// Output to GraphViz DOT format
	    let mut dot = String::from("digraph trace_graph {\n  node [shape=circle, fontsize=10];\n");
	    for (from, targets) in &edges {
	        for to in targets {
        	    dot.push_str(&format!("  \"0x{from:08X}\" -> \"0x{to:08X}\";\n"));
    	    }
	    }
	    dot.push_str("}\n");
		*/

	    //std::fs::write("trace_graph.dot", dot)?;
	    //println!("Wrote trace_graph.dot");

		//TODO: This graph should go in CVG.

		if crash {
			Ok(ExitKind::Crash)
		} else {
        	Ok(ExitKind::Ok)
		}
    }
}

pub fn fuzzer_thread_main(anchor_id: u32, input_size: usize) {
    let fuzz_buffer;
    unsafe {
        fuzz_buffer = fuzz_buffer_create(anchor_id);
    }
    // Create an observation channel using the signals map
    let observer = unsafe { StdMapObserver::new("cvg", &mut CVG) };

    // Feedback to rate the interestingness of an input
    let mut feedback = MaxMapFeedback::new(&observer);

    let calibration_stage = CalibrationStage::new(&feedback);
    let stats_stage = AflStatsStage::builder()
        .map_feedback(&feedback)
        .build()
        .unwrap();

    // A feedback to choose if an input is a solution or not
    let mut objective = feedback_and_fast!(
        // Look for crashes.
        CrashFeedback::new(),
        // We `and` the MaxMapFeedback to only end up with crashes that trigger new coverage.
        // We use the _fast variant to make sure it's not evaluated every time, even if the crash didn't trigger..
        // We have to give this one a name since it differs from the first map.
        MaxMapFeedback::with_name("on_crash", &observer)
    );

    // create a State from scratch
    let mut state = StdState::new(
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
    #[cfg(not(feature = "bloom_input_filter"))]
    let mut fuzzer = StdFuzzer::new(scheduler, feedback, objective);
    #[cfg(feature = "bloom_input_filter")]
    let filter = BloomInputFilter::new(10_000_000, 0.001);
    #[cfg(feature = "bloom_input_filter")]
    let mut fuzzer = StdFuzzerBuilder::new()
        .input_filter(filter)
        .scheduler(scheduler)
        .feedback(feedback)
        .objective(objective)
        .build();

    // Create the executor for an in-process function with just one observer
    let executor = FastDynExecutor::new(&state, fuzz_buffer);

    let mut executor = WithObservers::new(executor, tuple_list!(observer));

    // Generator of printable bytearrays
    let nz = NonZeroUsize::new(input_size)
        .expect("input_size must be non-zero");
    let mut generator = RandPrintablesGenerator::with_min_size(nz, nz);

    // Generate 8 initial inputs
    state
        .generate_initial_inputs(&mut fuzzer, &mut executor, &mut generator, &mut mgr, 8)
        .expect("Failed to generate the initial corpus");

    // Setup a mutational stage with a basic bytes mutator
    let mutator = HavocScheduledMutator::new(havoc_mutations());
    let mut stages = tuple_list!(
        calibration_stage,
        StdMutationalStage::new(mutator),
        stats_stage,
    );

    fuzzer
        .fuzz_loop(&mut stages, &mut executor, &mut state, &mut mgr)
        .expect("Error in the fuzzing loop");
}

#[no_mangle]
pub extern "C" fn fuzz_init(anchor_id: u32, cstr: *const c_char) -> u32 {
    // Safety: cstr must be a valid null-terminated C string
    let c_str = unsafe { CStr::from_ptr(cstr) };
    let r_str = c_str.to_str().unwrap(); // handle errors in production

    let input_size = (r_str.chars().filter(|&c| c == ',').count() + 1) * 4;

    let thread = std::thread::spawn(move || {
        fuzzer_thread_main(anchor_id, input_size);
    });

    return 1;
}