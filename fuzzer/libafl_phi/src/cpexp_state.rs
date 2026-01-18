#[cfg(feature = "std")]
use std::vec::Vec;
use core::{
    borrow::BorrowMut,
    cell::{Ref, RefMut},
    fmt::Debug,
    marker::PhantomData,
    time::Duration,
};
#[cfg(feature = "std")]
use std::{
    fs,
    path::{Path, PathBuf},
};

#[cfg(feature = "std")]
use libafl_bolts::core_affinity::{CoreId, Cores};
use libafl_bolts::{
    rands::{Rand, StdRand},
    serdeany::{NamedSerdeAnyMap, SerdeAnyMap},
};
use serde::{Deserialize, Serialize, de::DeserializeOwned};

// pub use stack::StageStack;
pub use libafl::state::StageStack;

use crate::cpexp_input::CPExpInput;
#[cfg(feature = "introspection")]
use crate::monitors::stats::ClientPerfStats;
use libafl::{
    Error, HasMetadata, HasNamedMetadata,
    corpus::{Corpus, CorpusId, HasCurrentCorpusId, HasTestcase, InMemoryCorpus, Testcase},
    events::{Event, EventFirer, EventWithStats, LogSeverity},
    feedbacks::StateInitializer,
    fuzzer::{Evaluator, ExecuteInputResult},
    generators::Generator,
    inputs::{self, Input, NopInput},
    stages::StageId,
};

use libafl::state::{
    HasCorpus, HasExecutions, HasImported, HasLastFoundTime, HasLastReportTime, HasMaxSize,
    HasRand, HasStartTime, HasSolutions, HasCurrentStageId, HasNestedStage, Stoppable,
};

use libafl::state::DEFAULT_MAX_SIZE;
// use libafl::state::LoadConfig;

use scirs2_optimize::global::{ 
    BayesianOptimizationOptions,
    BayesianOptimizer,
    Space
};
use scirs2_optimize::prelude::Parameter;
use scirs2_core::ndarray::Array1;

use crate::cpexp_input::ParamInput;
use crate::cpexp_input::EnvInput;

/*
    This is a copy of baby_fuzzer's StdState with two Bayesian Optimizers included!
*/

/// Struct that holds the options for input loading
#[cfg(feature = "std")]
pub struct CPExpLoadConfig<'a, I, S, Z> {
    /// Load Input even if it was deemed "uninteresting" by the fuzzer
    forced: bool,
    /// Function to load input from a Path
    loader: &'a mut dyn FnMut(&mut Z, &mut S, &Path) -> Result<I, Error>,
    /// Error if Input leads to a Solution.
    exit_on_solution: bool,
}

#[cfg(feature = "std")]
impl<I, S, Z> Debug for CPExpLoadConfig<'_, I, S, Z> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "LoadConfig {{}}")
    }
}

// #[derive(Serialize, Deserialize, Debug, Clone)]
#[derive(Serialize, Deserialize)]
#[serde(bound = "
        C: serde::Serialize + for<'a> serde::Deserialize<'a>,
        R: serde::Serialize + for<'a> serde::Deserialize<'a>,
        SC: serde::Serialize + for<'a> serde::Deserialize<'a>,
    ")]
pub struct CPExpState<C, I, R, SC> {
    /// RNG instance
    rand: R,
    /// How many times the executor ran the harness/target
    executions: u64,
    /// At what time the fuzzing started
    start_time: Duration,
    /// the number of new paths that imported from other fuzzers
    imported: usize,
    /// The corpus
    corpus: C,
    // Solutions corpus
    solutions: SC,
    /// Metadata stored for this state by one of the components
    metadata: SerdeAnyMap,
    /// Metadata stored with names
    named_metadata: NamedSerdeAnyMap,
    /// `MaxSize` testcase size for mutators that appreciate it
    max_size: usize,
    /// Performance statistics for this fuzzer
    #[cfg(feature = "introspection")]
    introspection_stats: ClientPerfStats,
    #[cfg(feature = "std")]
    /// Remaining initial inputs to load, if any
    remaining_initial_files: Option<Vec<PathBuf>>,
    #[cfg(feature = "std")]
    /// symlinks we have already traversed when loading `remaining_initial_files`
    dont_reenter: Option<Vec<PathBuf>>,
    #[cfg(feature = "std")]
    /// If inputs have been processed for multicore loading
    /// relevant only for `load_initial_inputs_multicore`
    multicore_inputs_processed: Option<bool>,
    /// The last time we reported progress (if available/used).
    /// This information is used by fuzzer `maybe_report_progress`.
    last_report_time: Option<Duration>,
    /// The last time something was added to the corpus
    last_found_time: Duration,
    /// The current index of the corpus; used to record for resumable fuzzing.
    corpus_id: Option<CorpusId>,
    /// Request the fuzzer to stop at the start of the next stage
    /// or at the beginning of the next fuzzing iteration
    stop_requested: bool,
    stage_stack: StageStack,
    phantom: PhantomData<I>,

    // Bayesian Optimizer to generate environment inputs and parameter values
    #[serde(skip)]
    optimizer: Option<BayesianOptimizer>,

    // Should we execute phi stage (true) or lambda stage (false)?
    optimize_params: bool

}

pub trait HasOptimizer {
    fn optimizer(&self) -> &Option<BayesianOptimizer>;
    fn optimizer_mut(&mut self) -> &mut Option<BayesianOptimizer>;
}

impl <C, I, R, SC> HasOptimizer for CPExpState<C, I, R, SC> {
    fn optimizer(&self) -> &Option<BayesianOptimizer> {
        &self.optimizer
    }
    fn optimizer_mut(&mut self) -> &mut Option<BayesianOptimizer> {
        &mut self.optimizer
    }
}

pub trait HasOptimizeParams {
    fn optimize_params(&self) -> bool;
    fn optimize_params_mut(&mut self) -> &mut bool;
}

impl <C, I, R, SC> HasOptimizeParams for CPExpState<C, I, R, SC> {
    fn optimize_params(&self) -> bool {
        self.optimize_params
    }
    fn optimize_params_mut(&mut self) -> &mut bool {
        &mut self.optimize_params
    }
}

impl<C, I, R, SC> HasRand for CPExpState<C, I, R, SC>
where
    R: Rand,
{
    type Rand = R;

    /// The rand instance
    #[inline]
    fn rand(&self) -> &Self::Rand {
        &self.rand
    }

    /// The rand instance (mutable)
    #[inline]
    fn rand_mut(&mut self) -> &mut Self::Rand {
        &mut self.rand
    }
}

impl<C, I, R, SC> HasCorpus<I> for CPExpState<C, I, R, SC>
where
    C: Corpus<I>,
{
    type Corpus = C;

    /// Returns the corpus
    #[inline]
    fn corpus(&self) -> &Self::Corpus {
        &self.corpus
    }

    /// Returns the mutable corpus
    #[inline]
    fn corpus_mut(&mut self) -> &mut Self::Corpus {
        &mut self.corpus
    }
}

impl<C, I, R, SC> HasTestcase<I> for CPExpState<C, I, R, SC>
where
    C: Corpus<I>,
{
    /// To get the testcase
    fn testcase(&self, id: CorpusId) -> Result<Ref<'_, Testcase<I>>, Error> {
        Ok(self.corpus().get(id)?.borrow())
    }

    /// To get mutable testcase
    fn testcase_mut(&self, id: CorpusId) -> Result<RefMut<'_, Testcase<I>>, Error> {
        Ok(self.corpus().get(id)?.borrow_mut())
    }
}

impl<C, I, R, SC> HasSolutions<I> for CPExpState<C, I, R, SC>
where
    C: Corpus<I>,
    I: Input,
    SC: Corpus<I>,
{
    type Solutions = SC;

    /// Returns the solutions corpus
    #[inline]
    fn solutions(&self) -> &SC {
        &self.solutions
    }

    /// Returns the solutions corpus (mutable)
    #[inline]
    fn solutions_mut(&mut self) -> &mut SC {
        &mut self.solutions
    }
}

impl<C, I, R, SC> HasMetadata for CPExpState<C, I, R, SC> {
    /// Get all the metadata into an [`hashbrown::HashMap`]
    #[inline]
    fn metadata_map(&self) -> &SerdeAnyMap {
        &self.metadata
    }

    /// Get all the metadata into an [`hashbrown::HashMap`] (mutable)
    #[inline]
    fn metadata_map_mut(&mut self) -> &mut SerdeAnyMap {
        &mut self.metadata
    }
}

impl<C, I, R, SC> HasNamedMetadata for CPExpState<C, I, R, SC> {
    /// Get all the metadata into an [`hashbrown::HashMap`]
    #[inline]
    fn named_metadata_map(&self) -> &NamedSerdeAnyMap {
        &self.named_metadata
    }

    /// Get all the metadata into an [`hashbrown::HashMap`] (mutable)
    #[inline]
    fn named_metadata_map_mut(&mut self) -> &mut NamedSerdeAnyMap {
        &mut self.named_metadata
    }
}

impl<C, I, R, SC> HasExecutions for CPExpState<C, I, R, SC> {
    /// The executions counter
    #[inline]
    fn executions(&self) -> &u64 {
        &self.executions
    }

    /// The executions counter (mutable)
    #[inline]
    fn executions_mut(&mut self) -> &mut u64 {
        &mut self.executions
    }
}

impl<C, I, R, SC> HasImported for CPExpState<C, I, R, SC> {
    /// Return the number of new paths that imported from other fuzzers
    #[inline]
    fn imported(&self) -> &usize {
        &self.imported
    }

    /// Return the number of new paths that imported from other fuzzers
    #[inline]
    fn imported_mut(&mut self) -> &mut usize {
        &mut self.imported
    }
}

impl<C, I, R, SC> HasLastFoundTime for CPExpState<C, I, R, SC> {
    /// Return the number of new paths that imported from other fuzzers
    #[inline]
    fn last_found_time(&self) -> &Duration {
        &self.last_found_time
    }

    /// Return the number of new paths that imported from other fuzzers
    #[inline]
    fn last_found_time_mut(&mut self) -> &mut Duration {
        &mut self.last_found_time
    }
}

impl<C, I, R, SC> HasLastReportTime for CPExpState<C, I, R, SC> {
    /// The last time we reported progress,if available/used.
    /// This information is used by fuzzer `maybe_report_progress`.
    fn last_report_time(&self) -> &Option<Duration> {
        &self.last_report_time
    }

    /// The last time we reported progress,if available/used (mutable).
    /// This information is used by fuzzer `maybe_report_progress`.
    fn last_report_time_mut(&mut self) -> &mut Option<Duration> {
        &mut self.last_report_time
    }
}

impl<C, I, R, SC> HasMaxSize for CPExpState<C, I, R, SC> {
    fn max_size(&self) -> usize {
        self.max_size
    }

    fn set_max_size(&mut self, max_size: usize) {
        self.max_size = max_size;
    }
}

impl<C, I, R, SC> HasStartTime for CPExpState<C, I, R, SC> {
    /// The starting time
    #[inline]
    fn start_time(&self) -> &Duration {
        &self.start_time
    }

    /// The starting time (mutable)
    #[inline]
    fn start_time_mut(&mut self) -> &mut Duration {
        &mut self.start_time
    }
}

impl<C, I, R, SC> HasCurrentCorpusId for CPExpState<C, I, R, SC> {
    fn set_corpus_id(&mut self, id: CorpusId) -> Result<(), Error> {
        self.corpus_id = Some(id);
        Ok(())
    }

    fn clear_corpus_id(&mut self) -> Result<(), Error> {
        self.corpus_id = None;
        Ok(())
    }

    fn current_corpus_id(&self) -> Result<Option<CorpusId>, Error> {
        Ok(self.corpus_id)
    }
}

impl<C, I, R, SC> Stoppable for CPExpState<C, I, R, SC> {
    fn request_stop(&mut self) {
        self.stop_requested = true;
    }

    fn discard_stop_request(&mut self) {
        self.stop_requested = false;
    }

    fn stop_requested(&self) -> bool {
        self.stop_requested
    }
}

impl<C, I, R, SC> HasCurrentStageId for CPExpState<C, I, R, SC> {
    fn set_current_stage_id(&mut self, idx: StageId) -> Result<(), Error> {
        self.stage_stack.set_current_stage_id(idx)
    }

    fn clear_stage_id(&mut self) -> Result<(), Error> {
        self.stage_stack.clear_stage_id()
    }

    fn current_stage_id(&self) -> Result<Option<StageId>, Error> {
        self.stage_stack.current_stage_id()
    }

    fn on_restart(&mut self) -> Result<(), Error> {
        self.stage_stack.on_restart()
    }
}

impl<C, I, R, SC> HasNestedStage for CPExpState<C, I, R, SC> {
    fn enter_inner_stage(&mut self) -> Result<(), Error> {
        self.stage_stack.enter_inner_stage()
    }

    fn exit_inner_stage(&mut self) -> Result<(), Error> {
        self.stage_stack.exit_inner_stage()
    }
}

#[cfg(feature = "std")]
impl<C, I, R, SC> CPExpState<C, I, R, SC>
where
    C: Corpus<I>,
    I: Input,
    R: Rand,
    SC: Corpus<I>,
{
    /// Decide if the state must load the inputs
    pub fn must_load_initial_inputs(&self) -> bool {
        self.corpus().count() == 0
            || (self.remaining_initial_files.is_some()
                && !self.remaining_initial_files.as_ref().unwrap().is_empty())
    }

    /// List initial inputs from a directory.
    fn next_file(&mut self) -> Result<PathBuf, Error> {
        loop {
            if let Some(path) = self.remaining_initial_files.as_mut().and_then(Vec::pop) {
                let filename = path.file_name().unwrap().to_string_lossy();
                if filename.starts_with('.')
                // || filename
                //     .rsplit_once('-')
                //     .is_some_and(|(_, s)| u64::from_str(s).is_ok())
                {
                    continue;
                }

                let attributes = fs::metadata(&path);

                if attributes.is_err() {
                    continue;
                }

                let attr = attributes?;

                if attr.is_file() && attr.len() > 0 {
                    return Ok(path);
                } else if attr.is_dir() {
                    let files = self.remaining_initial_files.as_mut().unwrap();
                    path.read_dir()?
                        .try_for_each(|entry| entry.map(|e| files.push(e.path())))?;
                } else if attr.is_symlink() {
                    let path = fs::canonicalize(path)?;
                    let dont_reenter = self.dont_reenter.get_or_insert_with(Default::default);
                    if dont_reenter.iter().any(|p| path.starts_with(p)) {
                        continue;
                    }
                    if path.is_dir() {
                        dont_reenter.push(path.clone());
                    }
                    let files = self.remaining_initial_files.as_mut().unwrap();
                    files.push(path);
                }
            } else {
                return Err(Error::iterator_end("No remaining files to load."));
            }
        }
    }

    /// Resets the state of initial files.
    fn reset_initial_files_state(&mut self) {
        self.remaining_initial_files = None;
        self.dont_reenter = None;
    }

    /// Sets canonical paths for provided inputs
    fn canonicalize_input_dirs(&mut self, in_dirs: &[PathBuf]) -> Result<(), Error> {
        if let Some(remaining) = self.remaining_initial_files.as_ref() {
            // everything was loaded
            if remaining.is_empty() {
                return Ok(());
            }
        } else {
            let files = in_dirs.iter().try_fold(Vec::new(), |mut res, file| {
                file.canonicalize().map(|canonicalized| {
                    res.push(canonicalized);
                    res
                })
            })?;
            self.dont_reenter = Some(files.clone());
            self.remaining_initial_files = Some(files);
        }
        Ok(())
    }

    /// Loads initial inputs from the passed-in `in_dirs`.
    /// If `forced` is true, will add all testcases, no matter what.
    /// This method takes a list of files.
    fn load_initial_inputs_custom_by_filenames<E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        manager: &mut EM,
        file_list: &[PathBuf],
        load_config: CPExpLoadConfig<I, Self, Z>,
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        if let Some(remaining) = self.remaining_initial_files.as_ref() {
            // everything was loaded
            if remaining.is_empty() {
                return Ok(());
            }
        } else {
            self.remaining_initial_files = Some(file_list.to_vec());
        }

        self.continue_loading_initial_inputs_custom(fuzzer, executor, manager, load_config)
    }

    fn load_file<E, EM, Z>(
        &mut self,
        path: &Path,
        manager: &mut EM,
        fuzzer: &mut Z,
        executor: &mut E,
        config: &mut CPExpLoadConfig<I, Self, Z>,
    ) -> Result<ExecuteInputResult, Error>
    where
        EM: EventFirer<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        log::info!("Loading file {} ...", path.display());
        let input = match (config.loader)(fuzzer, self, path) {
            Ok(input) => input,
            Err(err) => {
                log::error!(
                    "Skipping input that we could not load from {}: {err:?}",
                    path.display()
                );
                return Ok(ExecuteInputResult::None);
            }
        };
        if config.forced {
            let _ = fuzzer.add_input(self, executor, manager, input)?;
            Ok(ExecuteInputResult::Corpus)
        } else {
            let (res, _) = fuzzer.evaluate_input(self, executor, manager, &input)?;
            if res == ExecuteInputResult::None {
                fuzzer.add_disabled_input(self, input)?;
                log::warn!(
                    "Input {} was not interesting, adding as disabled.",
                    path.display()
                );
            }
            Ok(res)
        }
    }
    /// Loads initial inputs from the passed-in `in_dirs`.
    /// This method takes a list of files and a `LoadConfig`
    /// which specifies the special handling of initial inputs
    fn continue_loading_initial_inputs_custom<E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        manager: &mut EM,
        mut config: CPExpLoadConfig<I, Self, Z>,
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        loop {
            match self.next_file() {
                Ok(path) => {
                    let res = self.load_file(&path, manager, fuzzer, executor, &mut config)?;
                    if config.exit_on_solution && matches!(res, ExecuteInputResult::Solution) {
                        return Err(Error::invalid_corpus(format!(
                            "Input {} resulted in a solution.",
                            path.display()
                        )));
                    }
                }
                Err(Error::IteratorEnd(_, _)) => break,
                Err(e) => return Err(e),
            }
        }

        manager.fire(
            self,
            EventWithStats::with_current_time(
                Event::Log {
                    severity_level: LogSeverity::Debug,
                    message: format!("Loaded {} initial testcases.", self.corpus().count()), // get corpus count
                    phantom: PhantomData::<I>,
                },
                *self.executions(),
            ),
        )?;
        Ok(())
    }

    /// Recursively walk supplied corpus directories
    pub fn walk_initial_inputs<F>(
        &mut self,
        in_dirs: &[PathBuf],
        mut closure: F,
    ) -> Result<(), Error>
    where
        F: FnMut(&PathBuf) -> Result<(), Error>,
    {
        self.canonicalize_input_dirs(in_dirs)?;
        loop {
            match self.next_file() {
                Ok(path) => {
                    closure(&path)?;
                }
                Err(Error::IteratorEnd(_, _)) => break,
                Err(e) => return Err(e),
            }
        }
        self.reset_initial_files_state();
        Ok(())
    }
    /// Loads all intial inputs, even if they are not considered `interesting`.
    /// This is rarely the right method, use `load_initial_inputs`,
    /// and potentially fix your `Feedback`, instead.
    /// This method takes a list of files, instead of folders.
    pub fn load_initial_inputs_by_filenames<E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        manager: &mut EM,
        file_list: &[PathBuf],
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        self.load_initial_inputs_custom_by_filenames(
            fuzzer,
            executor,
            manager,
            file_list,
            CPExpLoadConfig {
                loader: &mut |_, _, path| I::from_file(path),
                forced: false,
                exit_on_solution: false,
            },
        )
    }

    /// Loads all intial inputs, even if they are not considered `interesting`.
    /// This is rarely the right method, use `load_initial_inputs`,
    /// and potentially fix your `Feedback`, instead.
    pub fn load_initial_inputs_forced<E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        manager: &mut EM,
        in_dirs: &[PathBuf],
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        self.canonicalize_input_dirs(in_dirs)?;
        self.continue_loading_initial_inputs_custom(
            fuzzer,
            executor,
            manager,
            CPExpLoadConfig {
                loader: &mut |_, _, path| I::from_file(path),
                forced: true,
                exit_on_solution: false,
            },
        )
    }
    /// Loads initial inputs from the passed-in `in_dirs`.
    /// If `forced` is true, will add all testcases, no matter what.
    /// This method takes a list of files, instead of folders.
    pub fn load_initial_inputs_by_filenames_forced<E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        manager: &mut EM,
        file_list: &[PathBuf],
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        self.load_initial_inputs_custom_by_filenames(
            fuzzer,
            executor,
            manager,
            file_list,
            CPExpLoadConfig {
                loader: &mut |_, _, path| I::from_file(path),
                forced: true,
                exit_on_solution: false,
            },
        )
    }

    /// Loads initial inputs from the passed-in `in_dirs`.
    pub fn load_initial_inputs<E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        manager: &mut EM,
        in_dirs: &[PathBuf],
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        self.canonicalize_input_dirs(in_dirs)?;
        self.continue_loading_initial_inputs_custom(
            fuzzer,
            executor,
            manager,
            CPExpLoadConfig {
                loader: &mut |_, _, path| I::from_file(path),
                forced: false,
                exit_on_solution: false,
            },
        )
    }

    /// Loads initial inputs from the passed-in `in_dirs`.
    /// Will return a `CorpusError` if a solution is found
    pub fn load_initial_inputs_disallow_solution<E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        manager: &mut EM,
        in_dirs: &[PathBuf],
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        self.canonicalize_input_dirs(in_dirs)?;
        self.continue_loading_initial_inputs_custom(
            fuzzer,
            executor,
            manager,
            CPExpLoadConfig {
                loader: &mut |_, _, path| I::from_file(path),
                forced: false,
                exit_on_solution: true,
            },
        )
    }

    fn calculate_corpus_size(&mut self) -> Result<usize, Error> {
        let mut count: usize = 0;
        loop {
            match self.next_file() {
                Ok(_) => {
                    count = count.saturating_add(1);
                }
                Err(Error::IteratorEnd(_, _)) => break,
                Err(e) => return Err(e),
            }
        }
        Ok(count)
    }
    /// Loads initial inputs by dividing the from the passed-in `in_dirs`
    /// in a multicore fashion. Divides the corpus in chunks spread across cores.
    pub fn load_initial_inputs_multicore<E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        manager: &mut EM,
        in_dirs: &[PathBuf],
        core_id: &CoreId,
        cores: &Cores,
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        if self.multicore_inputs_processed.unwrap_or(false) {
            self.continue_loading_initial_inputs_custom(
                fuzzer,
                executor,
                manager,
                CPExpLoadConfig {
                    loader: &mut |_, _, path| I::from_file(path),
                    forced: false,
                    exit_on_solution: false,
                },
            )?;
        } else {
            self.canonicalize_input_dirs(in_dirs)?;
            let corpus_size = self.calculate_corpus_size()?;
            log::info!(
                "{} total_corpus_size, {} cores",
                corpus_size,
                cores.ids.len()
            );
            self.reset_initial_files_state();
            self.canonicalize_input_dirs(in_dirs)?;
            if cores.ids.len() > corpus_size {
                log::info!("low intial corpus count ({corpus_size}), no parallelism required.");
            } else {
                let core_index = cores
                    .ids
                    .iter()
                    .enumerate()
                    .find(|(_, c)| *c == core_id)
                    .unwrap_or_else(|| panic!("core id {} not in cores list", core_id.0))
                    .0;
                let chunk_size = corpus_size.saturating_div(cores.ids.len());
                let mut skip = core_index.saturating_mul(chunk_size);
                let mut inputs_todo = chunk_size;
                let mut collected_inputs = Vec::new();
                log::info!(
                    "core = {}, core_index = {}, chunk_size = {}, skip = {}",
                    core_id.0,
                    core_index,
                    chunk_size,
                    skip
                );
                loop {
                    match self.next_file() {
                        Ok(path) => {
                            if skip != 0 {
                                skip = skip.saturating_sub(1);
                                continue;
                            }
                            if inputs_todo == 0 {
                                break;
                            }
                            collected_inputs.push(path);
                            inputs_todo = inputs_todo.saturating_sub(1);
                        }
                        Err(Error::IteratorEnd(_, _)) => break,
                        Err(e) => {
                            return Err(e);
                        }
                    }
                }
                self.remaining_initial_files = Some(collected_inputs);
            }
            self.multicore_inputs_processed = Some(true);
            return self
                .load_initial_inputs_multicore(fuzzer, executor, manager, in_dirs, core_id, cores);
        }
        Ok(())
    }
}

impl<C, I, R, SC> CPExpState<C, I, R, SC>
where
    C: Corpus<I>,
    I: Input,
    R: Rand,
    SC: Corpus<I>,
{

    fn generate_initial_internal<G, E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        generator: &mut G,
        manager: &mut EM,
        num: usize,
        forced: bool,
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        G: Generator<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        let mut added = 0;
        for _ in 0..num {
            let input = generator.generate(self)?;
            if forced {
                let _ = fuzzer.add_input(self, executor, manager, input)?;
                added += 1;
            } else {
                let (res, _) = fuzzer.evaluate_input(self, executor, manager, &input)?;
                if res != ExecuteInputResult::None {
                    added += 1;
                }
            }
        }
        manager.fire(
            self,
            EventWithStats::with_current_time(
                Event::Log {
                    severity_level: LogSeverity::Debug,
                    message: format!("Loaded {added} over {num} initial testcases"),
                    phantom: PhantomData,
                },
                *self.executions(),
            ),
        )?;
        Ok(())
    }

    /// Generate `num` initial inputs, using the passed-in generator and force the addition to corpus.
    pub fn generate_initial_inputs_forced<G, E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        generator: &mut G,
        manager: &mut EM,
        num: usize,
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        G: Generator<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {
        self.generate_initial_internal(fuzzer, executor, generator, manager, num, true)
    }

    /// Generate `num` initial inputs, using the passed-in generator.
    pub fn generate_initial_inputs<G, E, EM, Z>(
        &mut self,
        fuzzer: &mut Z,
        executor: &mut E,
        generator: &mut G,
        manager: &mut EM,
        num: usize,
        // inputs: &mut Vec<I>,
    ) -> Result<(), Error>
    where
        EM: EventFirer<I, Self>,
        G: Generator<I, Self>,
        Z: Evaluator<E, EM, I, Self>,
    {

        // for input in inputs.iter() {
        //     let _ = fuzzer.evaluate_input(self, executor, manager, input)?;
        // }

        // Ok(())
        self.generate_initial_internal(fuzzer, executor, generator, manager, num, false)
    }
}

impl<C, I, R, SC> CPExpState<C, I, R, SC>
where
    // C: Corpus<I>,
    // I: Input,
    R: Rand,
    // SC: Corpus<I>,
{
    /// Creates a new `State`, taking ownership of all of the individual components during fuzzing.
    pub fn new<F, O>(
        rand: R,
        corpus: C,
        solutions: SC,
        feedback: &mut F,
        objective: &mut O,
        bo: Option<BayesianOptimizer>,
        phi_first: bool,
    ) -> Result<Self, Error>
    where
        F: StateInitializer<Self>,
        O: StateInitializer<Self>,
        C: Serialize + DeserializeOwned,
        SC: Serialize + DeserializeOwned,
    {
        let mut state = Self {
            rand,
            executions: 0,
            imported: 0,
            start_time: libafl_bolts::current_time(),
            metadata: SerdeAnyMap::default(),
            named_metadata: NamedSerdeAnyMap::default(),
            corpus,
            solutions,
            max_size: DEFAULT_MAX_SIZE,
            stop_requested: false,
            #[cfg(feature = "introspection")]
            introspection_stats: ClientPerfStats::new(),
            #[cfg(feature = "std")]
            remaining_initial_files: None,
            #[cfg(feature = "std")]
            dont_reenter: None,
            last_report_time: None,
            last_found_time: libafl_bolts::current_time(),
            corpus_id: None,
            stage_stack: StageStack::default(),
            phantom: PhantomData,
            #[cfg(feature = "std")]
            multicore_inputs_processed: None,

            optimizer: bo,
            optimize_params: phi_first,

        };
        feedback.init_state(&mut state)?;
        objective.init_state(&mut state)?;
        Ok(state)
    }
}