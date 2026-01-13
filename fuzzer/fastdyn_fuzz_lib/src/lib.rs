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
    inputs::{HasTargetBytes, NopBytesConverter},
    mutators::{havoc_mutations::havoc_mutations, scheduled::HavocScheduledMutator},
    observers::StdMapObserver,
    schedulers::QueueScheduler,
    stages::{mutational::StdMutationalStage, AflStatsStage, CalibrationStage},
    state::{HasCorpus, HasExecutions, StdState},
    BloomInputFilter, StdFuzzerBuilder,
};

use std::fs::File;
use std::collections::{HashMap, HashSet};
use std::io::{self, Read, Write, BufReader};
use std::sync::mpsc::{Sender, Receiver, channel};
use std::thread::JoinHandle;

use libafl_bolts::{current_nanos, nonzero, rands::StdRand, tuples::tuple_list, AsSlice};
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
const MAP_SIZE: usize = 65536; // same as AFL
static mut CVG: [u8; MAP_SIZE] = [0; MAP_SIZE];

pub struct FuzzHandleInternal {
    pub input_rx: Receiver<u32>,        // C side receives inputs from Rust
    pub pc_tx: Sender<Vec<u32>>,        // C side sends PCs back to Rust
    pub thread: JoinHandle<()>,         // handle to the fuzzer thread
}


#[repr(C)]
pub struct FuzzHandle {
    inner: *mut FuzzHandleInternal,
}

struct FastDynExecutor<S> {
    phantom: PhantomData<S>,
    input_tx: Sender<u32>,
    pc_rx: Receiver<Vec<u32>>,
}

impl<S> FastDynExecutor<S> {
    pub fn new(_state: &S, input_tx: Sender<u32>, pc_rx: Receiver<Vec<u32>>) -> Self {
        Self {
            phantom: PhantomData,
            input_tx,
            pc_rx,
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

        // Number of 4-byte words
        let count = (buf.len() + 3) / 4;

        // Send count first
        self.input_tx.send(count as u32).unwrap();

        // Send each 4-byte chunk
        for chunk in buf.chunks(4) {
            let mut padded = [0u8; 4];
            padded[..chunk.len()].copy_from_slice(chunk);
            let word = u32::from_le_bytes(padded);
            self.input_tx.send(word).unwrap();
        }

//		println!("{:?}", buf);

        let pcs = self.pc_rx.recv().unwrap();

 //   	println!("Loaded {} PCs", pcs.len());


		// If deadbeef in trace, we must return Ok(ExitKind::Crash)
	    // Build transitions
		let mut crash = false;
	    let mut edges: HashMap<u32, HashSet<u32>> = HashMap::new();
	    for pair in pcs.windows(2) {
	        let from = pair[0];
	        let to = pair[1];
			if (to ==0xdeadbeef || from == 0xdeadbeef) {
					crash = true;
			}
    	    edges.entry(from).or_default().insert(to);
	    }

		
	    println!("Unique nodes: {}", edges.len());

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

		// Finally convert to AFL bitmap
		unsafe {
		    for (src, dsts) in &edges {
		        for dst in dsts {
        		    let idx = ((*src as usize) ^ (*dst as usize)) % MAP_SIZE;
		            CVG[idx] = CVG[idx].wrapping_add(1);
        		}
		    }
		}

		if crash {
			Ok(ExitKind::Crash)
		} else {
        	Ok(ExitKind::Ok)
		}
    }
}

pub fn fuzzer_thread_main(input_tx: Sender<u32>, pc_rx: Receiver<Vec<u32>>) {
    // Create an observation channel using the signals map
    let observer = unsafe { StdMapObserver::new("cvg", &mut CVG) };

    // Feedback to rate the interestingness of an input
    let mut feedback = MaxMapFeedback::new(&observer);

    let calibration_stage = CalibrationStage::new(&feedback);
    let stats_stage = AflStatsStage::builder()
        .map_observer(&observer)
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
        .bytes_converter(NopBytesConverter::default())
        .build(scheduler, feedback, objective)
        .unwrap();

    // Create the executor for an in-process function with just one observer
    let executor = FastDynExecutor::new(&state, input_tx, pc_rx);

    let mut executor = WithObservers::new(executor, tuple_list!(observer));

    // Generator of printable bytearrays of max size 32
    let mut generator = RandPrintablesGenerator::with_min_size(nonzero!(1), nonzero!(32));

    // Generate 8 initial inputs
    state
        .generate_initial_inputs(&mut fuzzer, &mut executor, &mut generator, &mut mgr, 8)
        .expect("Failed to generate the initial corpus");

    // Setup a mutational stage with a basic bytes mutator
    let mutator = HavocScheduledMutator::new(havoc_mutations());
    let mut stages = tuple_list!(
        calibration_stage,
        StdMutationalStage::new(mutator),
        stats_stage
    );

    fuzzer
        .fuzz_loop(&mut stages, &mut executor, &mut state, &mut mgr)
        .expect("Error in the fuzzing loop");
}

#[no_mangle]
pub extern "C" fn fuzz_init() -> *mut FuzzHandle {
    use std::sync::mpsc::channel;

    // Channels for simple u32 / Vec<u32> communication
    let (input_tx, input_rx) = channel::<u32>();          // Rust → C
    let (pc_tx, pc_rx) = channel::<Vec<u32>>();           // C → Rust

    // Spawn the fuzzer thread and move the correct ends into it
    let thread = std::thread::spawn(move || {
        fuzzer_thread_main(input_tx, pc_rx);
    });

    // Store the opposite ends for the C side
    let internal = Box::new(FuzzHandleInternal {
        input_rx,   // C receives input
        pc_tx,      // C sends PCs
        thread,
    });

    let handle = Box::new(FuzzHandle {
        inner: Box::into_raw(internal),
    });

    Box::into_raw(handle)
}

#[no_mangle]
pub extern "C" fn fuzz_receive_input(
    handle_ptr: *mut FuzzHandle,
    out_input: *mut u32,
) -> u32 {
    if handle_ptr.is_null() {
        return 0;
    }

    let internal = unsafe { &mut *(*handle_ptr).inner };

    // Block until the fuzzer thread sends an input
    match internal.input_rx.recv() {
        Ok(input) => {
            unsafe {
                *out_input = input;
            }
            1 // success
        }
        Err(_) => 0, // channel closed / error
    }
}

#[no_mangle]
pub extern "C" fn fuzz_submit_pcs(
    handle_ptr: *mut FuzzHandle,
    pcs_ptr: *const u32,
    pcs_len: u32,
) -> u32 {
    if handle_ptr.is_null() {
        return 0;
    }

    let internal = unsafe { &mut *(*handle_ptr).inner };

    if pcs_ptr.is_null() {
        return 0;
    }

    // Build Vec<u32> from C buffer
    let slice = unsafe { std::slice::from_raw_parts(pcs_ptr, pcs_len as usize) };
    let vec = slice.to_vec();

    match internal.pc_tx.send(vec) {
        Ok(_) => 1, // success
        Err(_) => 0,
    }
}

// AFL won't stop looping, so I think this will just freeze the program, but we shouldn't need to free, fuzzer should be lifetime
#[no_mangle]
pub extern "C" fn fuzz_free(handle_ptr: *mut FuzzHandle) {
    if handle_ptr.is_null() {
        return;
    }

    unsafe {
        // Take ownership of the outer handle
        let handle_box = Box::from_raw(handle_ptr);
        // Take ownership of the internal data
        let mut internal = Box::from_raw(handle_box.inner);

        // Dropping pc_tx and input_rx will eventually cause the fuzzer thread to see closed channels.

        // Join the fuzzer thread
        let _ = internal.thread.join();
        // `internal` is dropped here
    }
}

