use libafl::feedbacks::{Feedback, StateInitializer};
use libafl::observers::ObserversTuple;
use libafl::state::HasExecutions;
use libafl_bolts::Named;
use libafl::Error;
use libafl_bolts::tuples::{Handle, MatchNameRef};
use std::borrow::Cow;

use crate::PhysicalObserver;

pub struct PhysicalObjective {
    name: Cow<'static, str>,
    physical_observer_handle: Handle<PhysicalObserver>,
}

impl Named for PhysicalObjective {
    fn name(&self) -> &Cow<'static, str> {
        &self.name
    }
}

impl<S> StateInitializer<S> for PhysicalObjective {
    fn init_state(&mut self, _state: &mut S) -> Result<(), libafl::Error> {
        Ok(())
    }
}

impl PhysicalObjective {

    pub fn new() -> Self {

        Self {
            name: Cow::from("PhysicalObjective"),
            physical_observer_handle: Handle::new(Cow::from("PhysicalObserver")),
        }

    }

}

impl<EM, I, OT, S> Feedback<EM, I, OT, S> for PhysicalObjective
where 
    OT: ObserversTuple<I, S>,
{
    fn is_interesting(
            &mut self,
            _state: &mut S,
            _manager: &mut EM,
            _input: &I,
            observers: &OT,
            _exit_kind: &libafl::executors::ExitKind,
        ) -> Result<bool, Error> {

        println!("Hello from PhysicalObjective is_interesting!");

        // Get the latest robustness vector from the PhysicalObserver
        let physical_observer = observers.get(&self.physical_observer_handle).unwrap();
        let robustness_vec: &Vec<f64> = physical_observer.get_robustness_vec();

        // The objective will only consider negative robustness values as interesting
        for &robustness in robustness_vec.iter() {
            if robustness < 0.0 {
                return Ok(true);
            }
        }

        Ok(false)
    }
}