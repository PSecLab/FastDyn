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
    feedbacks::{CombinedFeedback, CrashFeedback, LogicFastAnd, MaxMapFeedback},
    fuzzer::{Fuzzer, StdFuzzer},
    generators::RandPrintablesGenerator,
    inputs::{HasTargetBytes, NopBytesConverter, ValueInput},
    mutators::{BitFlipMutator, ByteDecMutator, BytesDeleteMutator, BytesInsertMutator, BytesRandInsertMutator, ByteRandMutator, BytesSetMutator, BytesSwapMutator, ByteFlipMutator, ByteIncMutator, havoc_mutations::havoc_mutations, scheduled::HavocScheduledMutator},
    observers::StdMapObserver,
    schedulers::QueueScheduler,
    stages::{mutational::StdMutationalStage, AflStatsStage, CalibrationStage},
    state::{HasCorpus, HasExecutions, StdState},
    BloomInputFilter, StdFuzzerBuilder,
};

use std::fs::File;
use std::collections::{HashMap, HashSet};
use std::io::{self, Read, Write, BufReader};
use std::ffi::c_void;

use libafl_bolts::{current_nanos, nonzero, rands::StdRand, tuples::{tuple_list, tuple_list_type}, AsSlice};
/// Coverage map with explicit assignments due to the lack of instrumentation
static mut SIGNALS: [u8; 16] = [0; 16];
static mut SIGNALS_PTR: *mut u8 = &raw mut SIGNALS as _;
// TODO: This will break soon, fix me! See https://github.com/AFLplusplus/LibAFL/issues/2786
#[allow(static_mut_refs)] // only a problem in nightly
static SIGNALS_LEN: usize = unsafe { SIGNALS.len() };

const SHM_PATH: &str = "/tmp/iteration_count";

/// Assign a signal to the signals map
fn signals_set(idx: usize) {
    unsafe { write(SIGNALS_PTR.add(idx), 1) };
}

//This will hold the coverage from the run.
const MAP_SIZE: usize = 65536; // same as AFL
static mut CVG: [u8; MAP_SIZE] = [0; MAP_SIZE];


fn get_iteration_fuzz_counter() -> Result<(MmapMut, *mut u64, *mut u64), libafl::Error> {
    let path = Path::new(SHM_PATH);

    // Open or create the shared file
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .open(&path)?;

    // Ensure file is large enough for two 64-bit integers
    file.set_len((2 * std::mem::size_of::<u64>()) as u64)?;

    // Create shared writable mmap
    let mut mmap = unsafe { MmapMut::map_mut(&file)? };

    // Get raw pointers to the counters
    let base = mmap.as_mut_ptr() as *mut u64;
    let fuzz_counter = base;
    let input_counter = unsafe { base.add(1) };

    Ok((mmap, fuzz_counter, input_counter))
}

struct FastDynExecutor<S> {
    phantom: PhantomData<S>,
	mmap: MmapMut,
    fuzz_counter: *mut u64,
    input_counter: *mut u64,
}

impl<S> FastDynExecutor<S> {
    pub fn new(_state: &S) -> Self {
		let Ok((mmap, fuzz_counter, input_counter)) = get_iteration_fuzz_counter() else {
				panic!("failed to initialize shared memory");
		};
		unsafe {
            *input_counter = 0;
        }
        Self {
            phantom: PhantomData,
			mmap,
            fuzz_counter,
            input_counter,
        }
    }

	/// Increment the fuzz counter (number of total fuzz iterations)
    pub fn inc_fuzz_counter(&mut self) {
        unsafe {
            *self.fuzz_counter += 1;
        }
    }

    /// Increment the input counter (number of inputs processed in current iteration)
    pub fn inc_input_counter(&mut self) {
        unsafe {
            *self.input_counter += 1;
        }
    }

    /// Read the current fuzz counter
    pub fn read_fuzz_counter(&self) -> u64 {
        unsafe { *self.fuzz_counter }
    }

    /// Read the current input counter
    pub fn read_input_counter(&self) -> u64 {
        unsafe { *self.input_counter }
    }

    /// Reset both counters to 0
    pub fn reset_counters(&mut self) {
        unsafe {
            *self.input_counter = 0;
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
//		println!("{:?}", buf);

		// 1- Dump input for fastdyn
		let mut file = File::create("/tmp/input_fastdyn")?;
		file.write_all(buf)?;
		

		// Signal FastDyn
		self.inc_input_counter();


		// 2- Wait for fastdyn signal, veryslow but its fine
		while(self.read_input_counter() > self.read_fuzz_counter()) {};
		


		// 3- Read coverage
		// Open and read the file
    	let file = File::open("/data/fastdyn/trace_log.bin")?;
    	let mut reader = BufReader::new(file);

	    // Read all bytes
	    let mut data = Vec::new();
		reader.read_to_end(&mut data).map_err(|e| libafl::Error::unknown(format!("read_to_end: {e}")))?;


	    // Interpret as little-endian u32s
	    if data.len() % 4 != 0 {
	        eprintln!("Warning: file size ({}) not multiple of 4", data.len());
	    }

	    let pcs: Vec<u32> = data
    	    .chunks_exact(4)
        	.map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
	        .collect();

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

/*  Ignore these, this was from trying to explicitly type all of the AFL objects to return, will remove once library works
type FuzzInput = ValueInput<Vec<u8>>;
type FuzzObserver = StdMapObserver<'static, u8, false>;
type FuzzFeedback = MaxMapFeedback<FuzzObserver, FuzzObserver>;
type FuzzObjective = CombinedFeedback<
    CrashFeedback,
    FuzzFeedback,
    LogicFastAnd
>;
type FuzzState = StdState<
    InMemoryCorpus<FuzzInput>,
    FuzzInput,
    StdRand,
    OnDiskCorpus<FuzzInput>
>;
type FuzzExecutor = WithObservers<
    FastDynExecutor<FuzzState>,  // E
    FuzzInput,                   // I
    (FuzzObserver,),             // OT
    FuzzState                    // S
>;
type FuzzScheduler = QueueScheduler;
type FuzzFuzzer = StdFuzzer<
    FuzzScheduler,   // CS
    FuzzFeedback,    // F
    FuzzInput,       // IC
    FuzzInput,       // IF
    FuzzObjective    // OF
>;
type FuzzMgr = SimpleEventManager<
    FuzzInput,                   // I
    SimpleMonitor<fn(&str)>,     // MT
    FuzzState                    // S
>;
type FuzzStatsStage = AflStatsStage<
    InMemoryCorpus<FuzzInput>,  // C
    FuzzExecutor,               // E
    FuzzMgr,                    // EM
    FuzzInput,                  // I
    FuzzObserver,               // O
    FuzzState,                  // S
    FuzzFuzzer                  // Z
>;
type FuzzStages = tuple_list_type!(
    CalibrationStage<
        FuzzFeedback,    // C
        FuzzExecutor,    // E
        FuzzInput,       // I
        FuzzObserver,    // O
        (FuzzObserver,), // OT
        FuzzState        // S
    >,
    StdMutationalStage<
        FuzzExecutor,
        FuzzMgr,
        FuzzInput,
        FuzzInput,
        HavocScheduledMutator<tuple_list_type!(
            BitFlipMutator,
            ByteFlipMutator,
            ByteIncMutator,
            ByteDecMutator,
            ByteRandMutator,
            BytesDeleteMutator,
            BytesInsertMutator,
            BytesRandInsertMutator,
            BytesSetMutator,
            BytesSwapMutator
        )>,
        FuzzState,
        FuzzFuzzer
    >,
    FuzzStatsStage
);
pub struct FastDynFuzzer {
    pub fuzzer: FuzzFuzzer,
    pub stages: FuzzStages,
    pub executor: FuzzExecutor,
    pub state: FuzzState,
    pub mgr: FuzzMgr,
}
*/

pub struct FastDynFuzzer {
    pub fuzzer: StdFuzzer,
    pub stages: (),
    pub executor: FastDynExecutor,
    pub state: StdState,
    pub mgr: SimpleEventManager,
}

// Create fuzzer object
fn build_fuzzer() -> FastDynFuzzer {
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
    let executor = FastDynExecutor::new(&state);

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

    FastDynFuzzer {
        fuzzer,
        stages,
        executor,
        state,
        mgr,
    }
}

// Initialize the fuzzer, return a handle to C to use for fuzzing
#[no_mangle]
pub extern "C" fn fuzz_init() -> *mut FastDynFuzzer {
    let fuzzer = Box::new(build_fuzzer());
    Box::into_raw(fuzzer)
}

// Generate one input for C
#[no_mangle]
pub extern "C" fn fuzz_one_c(ptr: *mut FastDynFuzzer) -> u32 {
    let fuzz = unsafe { &mut *ptr };

    // Run target once
    fuzz.fuzzer
        .fuzz_one(&mut fuzz.stages, &mut fuzz.executor, &mut fuzz.state, &mut fuzz.mgr)
        .unwrap();

    // Get the generated input back and return, simple to do once problem of interfacing with C is resolved
    1
}