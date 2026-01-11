// mod banquo;

use std::collections::HashMap;
use banquo::{EvaluationError, Trace, evaluate, predicate};
use banquo::operators::{Always, And, Not, Implies};

// A predicate can only evaluate a named variable set, so we define a helper function to create
// hash maps for a single variable instead of repeating ourselves.
fn state(x_val: f64, y_val: f64) -> HashMap<&'static str, f64> {
    HashMap::from([("x", x_val), ("y", y_val)])
}

fn main() {
    let trace: Trace<HashMap<&str, f64>> = Trace::from([
        (0.0, state(99.0, 1.0)),
        (1.0, state(100.0, 2222.0)),
        (2.0, state(107.0, 3.0)),
        (3.0, state(111.0, 4.0)),
        (4.0, state(115.0, 5.0)),
    ]);

    let x_pred: banquo::Predicate = predicate!{ x <= 110.0 };
    let y_pred: banquo::Predicate = predicate!{ y <= 6.0 };
    let formula = Always::unbounded(And::new(x_pred, y_pred));
    let result: f64 = evaluate(&trace, &formula).unwrap();
    println!("Result: {}", result);
}