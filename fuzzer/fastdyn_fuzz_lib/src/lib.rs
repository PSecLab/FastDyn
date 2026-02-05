pub mod gz_state_parser;

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
    corpus::{CachedOnDiskCorpus, Corpus, InMemoryCorpus, OnDiskCorpus},
    Error,
    events::SimpleEventManager,
    executors::{Executor, ExitKind, WithObservers},
    feedback_and_fast,
    feedbacks::{CrashFeedback, MaxMapFeedback},
    fuzzer::{Fuzzer, StdFuzzer},
    generators::{Generator, RandPrintablesGenerator, RandBytesGenerator},
    inputs::{BytesInput, HasTargetBytes, Input, InputConverter, ValueInput},
    mutators::{ByteFlipMutator, havoc_mutations::havoc_mutations, MutationResult, Mutator, numeric::{DecMutator, IncMutator}, scheduled::HavocScheduledMutator, scheduled::SingleChoiceScheduledMutator},
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
    serdeany::SerdeAny,
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
    atomic::{AtomicU32},
};
use std::sync::{RwLock, Mutex, Arc};
use std::ptr;
use lazy_static::lazy_static;
use std::sync::{atomic::{AtomicBool, Ordering}};

static STOP_FLAG: AtomicBool = AtomicBool::new(false);

lazy_static::lazy_static! {
    static ref FUZZ_THREAD: Mutex<Option<JoinHandle<()>>> = Mutex::new(None);
}

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
    fn dump_bbl();
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

        unsafe { fuzz_buffer_write(self.fuzz_buffer, input_ptr); }

        const SPIN_ITERS: usize = 10_000;
        // timeout for 60 seconds using wall clock
        let start_time = std::time::Instant::now();
        let mut timed_out = false;
        let mut spins = 0;
        while unsafe { (*self.fuzz_buffer).status.load(Ordering::Acquire) } != 0 { // wait until C empties buffer, then its done
            if start_time.elapsed().as_secs() > 60 {
                timed_out = true;
                break;
            }

            if spins < SPIN_ITERS {
                std::hint::spin_loop();
            } else {
                std::thread::yield_now();
            }
            spins = spins + 1;
        }

        // if fuzzing target doesn't finish quickly, move to a slower less cpu-intensive option
        // const SPIN_ITERS: usize = 1_000;
        // const YIELD_ITERS: usize = 10_000;
        // let mut spins = 0;
        // loop {
        //     let finished = unsafe { fuzz_check_empty(self.fuzz_buffer) };
        //     if finished {
        //         break;
        //     }

        //     // Give firmware time to reach next anchor
        //     if spins < SPIN_ITERS {
        //         std::hint::spin_loop();
        //     } else if spins < YIELD_ITERS {
        //         std::thread::yield_now();
        //     } else {
        //         std::thread::sleep(std::time::Duration::from_micros(500));
        //     }

        //     spins = spins + 1;
        // }

        unsafe {
            if timed_out {
                let fuzz_file = Arc::new(Mutex::new(
                    OpenOptions::new()
                        .create(true)
                        .append(true)
                        .open("/root/rooney/FastDyn/fuzz_out/timeout.log")
                        .expect("Failed to open file"),
                ));

                let mut file = fuzz_file.lock().unwrap();
                writeln!(file, "Timeout on input {:?}", buf).unwrap();

                unsafe { dump_bbl(); }

                // set the stop flag to exit gracefully
                STOP_FLAG.store(true, Ordering::SeqCst);

                let stop = STOP_FLAG.load(Ordering::SeqCst);
                println!("Fuzzer stopping as requested: {}", stop);

                return Ok(ExitKind::Crash);
            }
            let value = fuzz_check_assert(self.fuzz_buffer);
            if value == 1 {
                return Ok(ExitKind::Crash);
            } else if value == 2 { // fatal error, exit
                let fuzz_file = Arc::new(Mutex::new(
                    OpenOptions::new()
                        .create(true)
                        .append(true)
                        .open("./fuzz_out/fuzzer.log")
                        .expect("Failed to open file"),
                ));

                let mut file = fuzz_file.lock().unwrap();
                writeln!(file, "Fatal error on input {:?}", buf).unwrap();

                unsafe { dump_bbl(); }

                // set the stop flag to exit gracefully
                STOP_FLAG.store(true, Ordering::SeqCst);

                return Ok(ExitKind::Crash);
            }
        }

        return Ok(ExitKind::Ok);

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
    }
}

use libafl::mutators::mutations::{
    BitFlipMutator,
    ByteIncMutator,
    ByteDecMutator,
    BytesSetMutator,
};


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

    let corpus_path = PathBuf::from("/root/rooney/FastDyn/fuzz_out/corpus");
    let crashes_path = PathBuf::from("/root/rooney/FastDyn/fuzz_out/crashes");

    //let mut corpus = OnDiskCorpus::<BytesInput>::new(corpus_path.clone()).unwrap();
    let mut corpus = InMemoryCorpus::new();
    let mut crash_corpus = OnDiskCorpus::<BytesInput>::new(crashes_path.clone()).unwrap();

    for entry in std::fs::read_dir(&corpus_path).unwrap() {
        let path = entry.unwrap().path();
        if path.is_file() {
            let bytes = std::fs::read(&path).unwrap();
            corpus.add(BytesInput::new(bytes).into()).unwrap();
        }
    }

    for entry in std::fs::read_dir(&crashes_path).unwrap() {
        let path = entry.unwrap().path();
        if path.is_file() {
            println!("Loading crash input from {:?}", path);
            let bytes = std::fs::read(&path).unwrap();
            crash_corpus.add(BytesInput::new(bytes).into()).unwrap();
        }
    }

    // create a State from scratch
    let mut state = StdState::new(
        // RNG
        StdRand::with_seed(current_nanos()),
        // Corpus that will be evolved, we keep it on disk so we can recover from
        corpus,
        // Corpus in which we store solutions (crashes in this example),
        // on disk so the user can get them after stopping the fuzzer
        crash_corpus,
        // States of the feedbacks.
        // The feedbacks can report the data that should persist in the State.
        &mut feedback,
        // Same for objective feedbacks
        &mut objective,
    )
    .unwrap();

    // let fuzz_file = Arc::new(Mutex::new(
    //     OpenOptions::new()
    //         .create(true)
    //         .append(true)
    //         .open("fuzzer.log")
    //         .expect("Failed to open file"),
    // ));

    // let mon = SimpleMonitor::new({
    //     let fuzz_file = fuzz_file.clone();  // clone Arc for closure
    //     move |s| {
    //         let mut f = fuzz_file.lock().unwrap(); // lock Mutex
    //         writeln!(f, "{s}").unwrap();      // write string + newline
    //     }
    // });


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
    // let nz = NonZeroUsize::new(input_size)
    let mut generator = RandBytesGenerator::with_min_size(
        NonZeroUsize::new(16).unwrap(),
        NonZeroUsize::new(262).unwrap(),
    );

    state // generate 8 initial inputs
        .generate_initial_inputs(&mut fuzzer, &mut executor, &mut generator, &mut mgr, 8)
        .expect("Failed to generate the initial corpus");


    // Setup a mutational stage with a basic bytes mutator

    let havoc_mutator = HavocScheduledMutator::new(havoc_mutations());

    let mut stages = tuple_list!(
        calibration_stage,
        StdMutationalStage::new(havoc_mutator),
        stats_stage,
    );

//     fuzzer
//         .fuzz_loop(&mut stages, &mut executor, &mut state, &mut mgr)
//         .expect("Error in the fuzzing loop")
    loop {
        let stop = STOP_FLAG.load(Ordering::SeqCst);
        assert!(stop == false, "Fuzzer stopping as requested");

        if let Err(err) = fuzzer.fuzz_one(&mut stages, &mut executor, &mut state, &mut mgr) {
            eprintln!("Fuzzing error: {:?}", err);
            break;
        }
    }
}

#[no_mangle]
pub extern "C" fn fuzz_init(anchor_id: u32, cstr: *const c_char) -> u32 {
    // Safety: cstr must be a valid null-terminated C string
    let c_str = unsafe { CStr::from_ptr(cstr) };
    let r_str = c_str.to_str().unwrap(); // handle errors in production

    let input_size = (r_str.chars().filter(|&c| c == ',').count() + 1) * 4;

    STOP_FLAG.store(false, Ordering::SeqCst);

    let handle = std::thread::spawn(move || {
        fuzzer_thread_main(anchor_id, input_size);
    });

    *FUZZ_THREAD.lock().unwrap() = Some(handle);

    return 1;
}

#[no_mangle]
pub extern "C" fn fuzz_stop() -> u32 {
    STOP_FLAG.store(true, Ordering::SeqCst);

    if let Some(handle) = FUZZ_THREAD.lock().unwrap().take() {
        handle.join().unwrap();
    }

    return 1;
}

#[no_mangle]
pub extern "C" fn fuzz_is_running() -> u32 {
    // stop flag is false means running
    if STOP_FLAG.load(Ordering::SeqCst) {
        return 0;
    } else {
        return 1;
    }
}
