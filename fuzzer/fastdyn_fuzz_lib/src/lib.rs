#[cfg(windows)]
use std::ptr::write_volatile;
use std::{marker::PhantomData, path::PathBuf};
use std::fs::OpenOptions;

#[cfg(feature = "tui")]
use libafl::monitors::tui::TuiMonitor;
#[cfg(not(feature = "tui"))]
use libafl::monitors::SimpleMonitor;
use libafl::{
    corpus::{Corpus, InMemoryCorpus, OnDiskCorpus},
    events::SimpleEventManager,
    executors::{Executor, ExitKind, WithObservers},
    feedback_and_fast,
    feedbacks::{CrashFeedback, MaxMapFeedback},
    fuzzer::Fuzzer,
    generators::RandPrintablesGenerator,
    inputs::{BytesInput, HasTargetBytes},
    mutators::{havoc_mutations::havoc_mutations, scheduled::HavocScheduledMutator},
    observers::StdMapObserver,
    schedulers::QueueScheduler,
    stages::{mutational::StdMutationalStage, AflStatsStage, CalibrationStage},
    state::{HasCorpus, HasExecutions, StdState},
    BloomInputFilter, StdFuzzerBuilder,
};
use libafl_bolts::{
    current_nanos,
    rands::StdRand,
    tuples::tuple_list,
    AsSlice,
};
use core::num::NonZeroUsize;
use std::io::Write;
use std::thread::JoinHandle;
use std::ffi::CStr;
use std::os::raw::c_char;
use std::sync::{
    atomic::{AtomicBool, Ordering},
};
use std::sync::{Mutex, Arc};
use bincode;
use serde::ser;

static STOP_FLAG: AtomicBool = AtomicBool::new(false);

lazy_static::lazy_static! {
    static ref FUZZ_THREAD: Mutex<Option<JoinHandle<()>>> = Mutex::new(None);
}

//This will hold the coverage from the run.
const MAP_SIZE: usize = 65536; // same as AFL, make sure the definition of this size in C is the same
#[no_mangle]
pub static mut CVG: [u8; MAP_SIZE] = [0; MAP_SIZE];

static STATE_PATH: &str = "/root/rooney/FastDyn/fuzz_out/state.bin";

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
    fn fuzz_check_assert(fb: *mut FuzzBuffer) -> u32;
    fn dump_bbl();
}

struct FastDynExecutor<S> {
    phantom: PhantomData<S>,
    fuzz_buffer: *mut FuzzBuffer,
    crashed: bool,
}

impl<S> FastDynExecutor<S> {
    pub fn new(_state: &S, fuzz_buffer: *mut FuzzBuffer) -> Self {
        Self {
            phantom: PhantomData,
            fuzz_buffer,
            crashed: false,
        }
    }
}

impl<EM, I, S, Z> Executor<EM, I, S, Z> for FastDynExecutor<S>
where
    S: HasCorpus<I> + HasExecutions + ser::Serialize,
    I: HasTargetBytes,
{
    fn run_target(
        &mut self,
        _fuzzer: &mut Z,
        state: &mut S,
        _mgr: &mut EM,
        input: &I,
    ) -> Result<ExitKind, libafl::Error> {
        if self.crashed {
            // serialize and panic only after libafl observes previous input outcome
            let encoded = bincode::serialize(&state).expect("Couldn't serialize state");
            std::fs::write(STATE_PATH, encoded).expect("Couldn't write serialized state to file");

            panic!("Fuzzer reached hard fault");
        }

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
        let mut spins = 0;
        // timeout for 60 seconds using wall clock
        let start_time = std::time::Instant::now();
        let mut timed_out = false;
        while unsafe { (*self.fuzz_buffer).status.load(Ordering::Acquire) } != 0 { // wait until C empties buffer, then its done
            // if start_time.elapsed().as_secs() > 60 {
            //     timed_out = true;
            //     break;
            // }

            if spins < SPIN_ITERS {
                std::hint::spin_loop();
            } else {
                std::thread::yield_now();
            }
            spins = spins + 1;
        }

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

                // set the stop flag to exit gracefully
                STOP_FLAG.store(true, Ordering::SeqCst);

                return Ok(ExitKind::Crash);
            }
            let value = fuzz_check_assert(self.fuzz_buffer);
            if value == 1 {
                return Ok(ExitKind::Crash);
            } else if value == 2 { // fatal error
                let fuzz_file = Arc::new(Mutex::new(
                    OpenOptions::new()
                        .create(true)
                        .append(true)
                        .open("/root/rooney/FastDyn/fuzz_out/fuzzer.log")
                        .expect("Failed to open file"),
                ));

                let mut file = fuzz_file.lock().unwrap();
                writeln!(file, "Fatal error on input {:?}", buf).unwrap();
                dump_bbl();

                self.crashed = true;
                return Ok(ExitKind::Crash);
            }
            return Ok(ExitKind::Ok);
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

    let corpus_path = PathBuf::from("/root/rooney/FastDyn/fuzz_out/corpus");
    let crashes_path = PathBuf::from("/root/rooney/FastDyn/fuzz_out/crashes");

    //let mut corpus = OnDiskCorpus::<BytesInput>::new(corpus_path.clone()).unwrap();
    let mut corpus = InMemoryCorpus::new();
    let crash_corpus = OnDiskCorpus::<BytesInput>::new(crashes_path.clone()).unwrap();

    for entry in std::fs::read_dir(&corpus_path).unwrap() {
        let path = entry.unwrap().path();
        if path.is_file() {
            let bytes = std::fs::read(&path).unwrap();
            corpus.add(BytesInput::new(bytes).into()).unwrap();
        }
    }

    let mut state = if std::path::Path::new(STATE_PATH).exists() {
        let bytes = std::fs::read(STATE_PATH).expect("Couldn't open state");
        bincode::deserialize(&bytes).expect("Couldn't deserialize state")
    } else {
        StdState::new(
            StdRand::with_seed(current_nanos()),
            corpus,
            crash_corpus,
            &mut feedback,
            &mut objective,
        )
        .unwrap()
    };

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

    // If corpus is empty, generate random inputs
    if state.corpus().count() == 0 {
        let nz = NonZeroUsize::new(input_size)
            .expect("input_size must be non-zero");
        let mut generator = RandPrintablesGenerator::with_min_size(nz, nz); // sized based on arguments to anchor
        //let mut generator = RandPrintablesGenerator::with_min_size(NonZeroUsize::new(8).expect(""), NonZeroUsize::new(12).expect("")); // fixed size

        state
            .generate_initial_inputs(&mut fuzzer, &mut executor, &mut generator, &mut mgr, 8)
            .expect("Failed to generate the initial corpus");
    }

    // Setup a mutational stage with a basic bytes mutator

    let havoc_mutator = HavocScheduledMutator::new(havoc_mutations());

    let mut stages = tuple_list!(
        calibration_stage,
        StdMutationalStage::new(havoc_mutator),
        stats_stage,
    );

    loop {
        if STOP_FLAG.load(Ordering::SeqCst) {
            unsafe { dump_bbl(); }
            let encoded = bincode::serialize(&state).expect("Coudln't serialize state");
            std::fs::write(STATE_PATH, encoded).expect("Couldn't write serialized state to file");
            panic!("Fuzzer stopping as requested");
        }

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
