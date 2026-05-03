#[cfg(windows)]
use std::ptr::write_volatile;
use std::{
    fs::{self, OpenOptions},
    io::Write,
    marker::PhantomData,
    path::PathBuf,
    ptr,
    sync::{
        atomic::{AtomicBool, AtomicPtr, AtomicU32, AtomicU64, AtomicUsize, Ordering},
        Mutex,
    },
    thread::JoinHandle,
};

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
use bincode;
use serde::ser;

static STOP_FLAG: AtomicBool = AtomicBool::new(false);
static ASSERT_STATUS: AtomicU32 = AtomicU32::new(ASSERT_NONE);
static CURRENT_INPUT_PTR: AtomicPtr<u8> = AtomicPtr::new(ptr::null_mut());
static CURRENT_INPUT_LEN: AtomicUsize = AtomicUsize::new(0);
static NEXT_INPUT_EPOCH: AtomicU64 = AtomicU64::new(1);
static PUBLISHED_INPUT_EPOCH: AtomicU64 = AtomicU64::new(0);
static COMPLETED_INPUT_EPOCH: AtomicU64 = AtomicU64::new(0);

lazy_static::lazy_static! {
    static ref FUZZ_THREAD: Mutex<Option<JoinHandle<()>>> = Mutex::new(None);
}

//This will hold the coverage from the run.
const MAP_SIZE: usize = 65536; // same as AFL, make sure the definition of this size in C is the same
#[no_mangle]
pub static mut CVG: [u8; MAP_SIZE] = [0; MAP_SIZE];

static STATE_PATH: &str = "fastdyn_work/state.bin";

const ASSERT_NONE: u32 = 0;
const ASSERT_RECOVERABLE: u32 = 1;
const ASSERT_FATAL: u32 = 2;

extern "C" {
    fn fuzz_dump_bbl();
}

fn spin_wait_until(mut done: impl FnMut() -> bool) {
    const SPIN_ITERS: usize = 10_000;
    let mut spins = 0;

    while !done() {
        if spins < SPIN_ITERS {
            std::hint::spin_loop();
            spins += 1;
        } else {
            std::thread::yield_now();
        }
    }
}

fn publish_input(epoch: u64, buf: &[u8]) {
    ASSERT_STATUS.store(ASSERT_NONE, Ordering::Release);
    CURRENT_INPUT_LEN.store(buf.len(), Ordering::Relaxed);
    CURRENT_INPUT_PTR.store(buf.as_ptr() as *mut u8, Ordering::Release);
    PUBLISHED_INPUT_EPOCH.store(epoch, Ordering::Release);
}

fn clear_input() {
    CURRENT_INPUT_PTR.store(ptr::null_mut(), Ordering::Release);
    CURRENT_INPUT_LEN.store(0, Ordering::Relaxed);
}

fn wait_until_complete(epoch: u64) {
    spin_wait_until(|| COMPLETED_INPUT_EPOCH.load(Ordering::Acquire) >= epoch);
}

fn take_assert_status() -> u32 {
    ASSERT_STATUS.swap(ASSERT_NONE, Ordering::AcqRel)
}

fn log_fatal_input(buf: &[u8]) {
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open("./fastdyn_work/fuzzer.log")
        .expect("Failed to open file");

    writeln!(file, "Fatal error on input {:?}", buf).unwrap();

    unsafe {
        fuzz_dump_bbl();
    }
}

#[no_mangle]
pub extern "C" fn fuzz_libafl_wait_next(
    after_epoch: u64,
    data: *mut *const u8,
    len: *mut usize,
) -> u64 {
    spin_wait_until(|| PUBLISHED_INPUT_EPOCH.load(Ordering::Acquire) > after_epoch);

    let epoch = PUBLISHED_INPUT_EPOCH.load(Ordering::Acquire);
    let input_ptr = CURRENT_INPUT_PTR.load(Ordering::Acquire) as *const u8;
    let input_len = CURRENT_INPUT_LEN.load(Ordering::Relaxed);

    if data.is_null() || len.is_null() || input_ptr.is_null() {
        return 0;
    }

    unsafe {
        *data = input_ptr;
        *len = input_len;
    }

    epoch
}

#[no_mangle]
pub extern "C" fn fuzz_libafl_complete(epoch: u64) {
    COMPLETED_INPUT_EPOCH.fetch_max(epoch, Ordering::AcqRel);
}

#[no_mangle]
pub extern "C" fn fuzz_libafl_report_assert(fatal: bool) {
    let status = if fatal {
        ASSERT_FATAL
    } else {
        ASSERT_RECOVERABLE
    };

    ASSERT_STATUS.fetch_max(status, Ordering::AcqRel);
}

struct FastDynExecutor<S> {
    phantom: PhantomData<S>,
    crashed: bool,
}

impl<S> FastDynExecutor<S> {
    pub fn new(_state: &S) -> Self {
        Self {
            phantom: PhantomData,
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
        let epoch = NEXT_INPUT_EPOCH.fetch_add(1, Ordering::Relaxed);

        publish_input(epoch, buf);
        wait_until_complete(epoch);
        clear_input();

        match take_assert_status() {
            ASSERT_RECOVERABLE => Ok(ExitKind::Crash),
            ASSERT_FATAL => {
                log_fatal_input(buf);
                self.crashed = true;
                Ok(ExitKind::Crash)
            }
            _ => Ok(ExitKind::Ok),
        }
    }
}

pub fn fuzzer_thread_main() {
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

    let corpus_path = PathBuf::from("fastdyn_work/corpus");
    let crashes_path = PathBuf::from("fastdyn_work/crashes");

    fs::create_dir_all(&corpus_path).expect("Couldn't create corpus directory");
    fs::create_dir_all(&crashes_path).expect("Couldn't create crashes directory");

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
    let executor = FastDynExecutor::new(&state);
    let mut executor = WithObservers::new(executor, tuple_list!(observer));

    // If corpus is empty, generate random inputs
    if state.corpus().count() == 0 {
        let mut generator = RandPrintablesGenerator::with_min_size(
            NonZeroUsize::new(10).expect(""),
            NonZeroUsize::new(60).expect(""),
        );
        println!("Generating new inputs");
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
            unsafe { fuzz_dump_bbl(); }
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
pub extern "C" fn fuzz_libAFL_init() -> u32 {
    STOP_FLAG.store(false, Ordering::SeqCst);

    let handle = std::thread::spawn(move || {
        fuzzer_thread_main();
    });

    *FUZZ_THREAD.lock().unwrap() = Some(handle);

    return 1;
}
