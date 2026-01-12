// LibAFL style modules
mod executor;
mod observer;
mod mutator;
mod feedback;

use executor::Executor;


fn main() {
    let mut exec = Executor::new();
    exec.execute_target();
    std::thread::sleep(std::time::Duration::from_secs(10));
    exec.kill_target();
}