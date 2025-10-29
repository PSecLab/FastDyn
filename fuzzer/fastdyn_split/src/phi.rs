/// This file contains the implementation of the phi stage observer, feedback,
/// and objective for the FastDyn fuzzer as well as the mutator for the stage.
///
/// The phi stage API acts as a bridge between the main fuzzer loop and the
/// psy-taliro instance that is being used to do physical fuzzing for
/// requirements falsification of cyber-physical systems (CPS).
///
/// Author: Mike Rooney

use libafl::mutators::Mutator;
use libafl::rand::RandState;
use libafl::feedbacks::Feedback;
use libafl::events::EventManager;
use libafl::observers::Observer;
use libafl::observers::ObserversTuple;
use libafl::inputs::BytesInput;
use libafl::state::HasCorpus;
use libafl::corpus::{Corpus, CorpusId, InMemoryCorpus};
use libafl::Error;

#[derive(Clone, Debug)]
pub struct FlightEnvInput {
    // Flight controller commands
    pub throttle: u8,
    pub pitch: i16,
    pub roll: i16,
    pub yaw: i16,

    // Environment parameters (e.g., wind, temperature)
    pub wind_speed: f32,
    pub wind_direction: f32,
    pub temperature: f32,
}

pub struct FlightEnvCorpus {
    inner: InMemoryCorpus<FlightEnvInput>,
}

impl FlightEnvCorpus {
    pub fn new() -> Self {
        Self {
            inner: InMemoryCorpus::new(),
        }
    }
}

impl Corpus<FlightEnvInput> for FlightEnvCorpus {
    fn add(&mut self, input: FlightEnvInput) -> Result<CorpusId, Error> {
        self.inner.add(input)
    }

    fn get(&self, id: CorpusId) -> Option<&FlightEnvInput> {
        self.inner.get(id)
    }

    fn get_mut(&mut self, id: CorpusId) -> Option<&mut FlightEnvInput> {
        self.inner.get_mut(id)
    }

    fn count(&self) -> usize {
        self.inner.count()
    }

    fn idx(&self, id: CorpusId) -> Option<usize> {
        self.inner.idx(id)
    }

    fn replace(&mut self, id: CorpusId, input: FlightEnvInput) -> Result<(), Error> {
        self.inner.replace(id, input)
    }
}

pub struct TraceObserver {
    // Define fields to track the physical state
    // For example, tracking positions or velocities
    pub position: f64,
    pub velocity: f64,
}

impl TraceObserver {
    pub fn new() -> Self {
        Self {
            // TODO: Add fields to reflect trace state
            // TODO: May have to have the trace be collected by the executor and passed to the observer (remove psy-taliro dependency here)
            position: 0.0,
            velocity: 0.0,
        }
    }
}

impl<S> Observer<FlightEnvInput, S> for TraceObserver
where
    S: HasCorpus<FlightEnvInput>,
{
    type Value = (f64, f64); // Tuple holding position and velocity

    fn pre_exec(&mut self, _state: &mut S, _input: &FlightEnvInput) -> Result<(), Error> {
        // Reset state before each execution
        self.position = 0.0;
        self.velocity = 0.0;
        Ok(())
    }

    fn post_exec(
        &mut self,
        _state: &mut S,
        _input: &FlightEnvInput,
        _exit_kind: &libafl::executors::ExitKind,
    ) -> Result<(), Error> {
        // Update state after each execution
        // Here, you would extract the actual metrics from the simulation
        self.position += 1.0; // Placeholder logic
        self.velocity += 0.5; // Placeholder logic
        Ok(())
    }

    fn observe(&self) -> &Self::Value {
        // Return the observed state
        &(self.position, self.velocity)
    }
}

/// Feedback that labels inputs as interesting if they lower the robustness value or make it negative
pub struct PhiFeedback<'a> {
    trace_observer: &'a TraceObserver,
    prev_robustness: f64,
}

impl<'a> PhiFeedback<'a> {
    pub fn new(trace_observer: &'a TraceObserver) -> Self {
        Self { trace_observer, prev_robustness: f64::INFINITY }
    }
}

impl<'a, S> Feedback<FlightEnvInput, S> for PhiFeedback<'a>
where
    S: HasCorpus<FlightEnvInput>,
{
    fn is_interesting(
        &mut self,
        _state: &mut S,
        _mgr: &mut EventManager<S>,
        _input: &FlightEnvInput,
        _observers: &impl ObserversTuple<FlightEnvInput, S>,
    ) -> bool {
        // TODO: Get robustness from the psy-taliro monitor
        let robustness = 0.04; // Placeholder value
        // TODO: Update prev_robustness appropriately
        self.prev_robustness = robustness;
        robustness < 0.0 || robustness < self.prev_robustness
    }
}

pub struct PsyTaliroMutator;

impl Mutator<FlightEnvInput, RandState> for PsyTaliroMutator {
    fn mutate(
        &mut self,
        input: &mut FlightEnvInput,
        _stage_idx: usize,
        _rand: &mut RandState,
    ) -> Result<(), Error> {
        // Call your psy-taliro optimizer here
        // and update the fields of `input` accordingly

        let new_input = call_psy_taliro(); // returns FlightEnvInput
        *input = new_input;

        Ok(())
    }
}

// Example stub for your optimizer integration
fn call_psy_taliro() -> FlightEnvInput {
    // TODO: call your actual optimizer process
    FlightEnvInput {
        throttle: 128,
        pitch: 5,
        roll: 0,
        yaw: -3,
        wind_speed: 2.0,
        wind_direction: 45.0,
        temperature: 25.0,
    }
}


// will use MutationalStage::new(psy_taliro_mutator, ... ) to create the stage