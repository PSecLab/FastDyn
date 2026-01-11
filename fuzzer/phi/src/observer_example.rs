// LibAFL style modules
mod executor;
mod observer;
mod mutator;
mod feedback;

use observer::Observer;

fn main() {
    let mut observer: Observer = Observer::new(
        0.1, // time step
        10.0, // sim time limit
        "/home/ere/fire/FastDyn/fuzzer/phi/target/debug/trace_recorder", // recorder binary path
        "./my_traces" // trace log directory
    );

    let success: bool = observer.pre_exec();
    if success {
        std::thread::sleep(std::time::Duration::from_secs_f64(15.0));
        observer.post_exec();
    }
}