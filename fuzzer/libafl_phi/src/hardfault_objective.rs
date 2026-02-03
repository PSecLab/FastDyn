use libafl::feedbacks::{Feedback, StateInitializer};
use libafl::observers::ObserversTuple;
use libafl::state::HasExecutions;
use libafl_bolts::Named;
use libafl::Error;
use libafl_bolts::tuples::{Handle, MatchNameRef};
use std::borrow::Cow;

use crate::hardfault_observer::HardFaultObserver;

pub struct HardFaultObjective {
    name: Cow<'static, str>,
    hardfault_observer_handle: Handle<HardFaultObserver>,
}

impl Named for HardFaultObjective {
    fn name(&self) -> &Cow<'static, str> {
        &self.name
    }
}

impl<S> StateInitializer<S> for HardFaultObjective {
    fn init_state(&mut self, _state: &mut S) -> Result<(), libafl::Error> {
        Ok(())
    }
}

impl HardFaultObjective {

    pub fn new() -> Self {

        Self {
            name: Cow::from("HardFaultObjective"),
            hardfault_observer_handle: Handle::new(Cow::from("HardFaultObserver")),
        }

    }

}

impl<EM, I, OT, S> Feedback<EM, I, OT, S> for HardFaultObjective
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

        // println!("Hello from HardFaultObjective is_interesting!");

        // // Get the latest robustness vector from the HardFaultObserver
        // let hardfault_observer = observers.get(&self.hardfault_observer_handle).unwrap();
        
        // if hardfault_observer.get_faulting_pc() != 0 {
        //     println!("Hard fault detected at PC: 0x{:08X}", hardfault_observer.get_faulting_pc());
        //     return Ok(true);
        // }

        Ok(false)
    }
}