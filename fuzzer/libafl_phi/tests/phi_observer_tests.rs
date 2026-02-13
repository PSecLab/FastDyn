use std::fs::{self, File};
use std::io::{BufRead, BufReader, Write};
use std::path::PathBuf;

use banquo_parser::{parse_formula, Formula, ParsedFormula, Trace};

/// Helper to create a temporary working directory for tests and return to the
/// original directory afterwards.
fn with_temp_cwd<F: FnOnce(&PathBuf)>(test_fn: F) {
    let original_dir = std::env::current_dir().expect("get current dir");
    let temp_dir = original_dir.join("target").join("phi_observer_test_tmp");

    if temp_dir.exists() {
        fs::remove_dir_all(&temp_dir).expect("remove old temp dir");
    }
    fs::create_dir_all(&temp_dir).expect("create temp dir");

    std::env::set_current_dir(&temp_dir).expect("chdir temp");
    test_fn(&temp_dir);
    std::env::set_current_dir(&original_dir).expect("restore cwd");
}

#[test]
fn loads_formulas_from_text_file_and_evaluates() {
    with_temp_cwd(|_dir| {
        // 1) Create an STL formulas file in the temp working directory.
        let mut formulas_file = File::create("stl_formulas.txt").expect("create formulas file");
        // For now we only check that the file is readable. Parsing will fail
        // because the current parser syntax does not yet accept `and` between
        // inequalities; this test just proves that we can read the file
        // contents that `PhysicalObserver` will later feed into `parse_formula`.
        writeln!(formulas_file, "3.1*x <= 0.5*y").unwrap();

        // 2) Load and parse the formula using banquo-parser directly, just like
        //    `PhysicalObserver` does under the hood.
        let contents =
            fs::read_to_string("stl_formulas.txt").expect("read formulas file written by test");
        let line = contents.lines().next().unwrap().trim();
        assert_eq!(line, "3.1*x <= 0.5*y");

        // As a sanity check, round-trip this line through `parse_formula`.
        let formula: ParsedFormula = parse_formula(line).expect("parse simple STL-style formula");

        // 3) Build a trace that provides all variables used in the formula.
        let mut trace: Trace<std::collections::HashMap<String, f64>> = Trace::new();
        trace.insert(
            0.0,
            std::collections::HashMap::from([
                ("x".to_string(), 0.1_f64),
                ("y".to_string(), 1.0_f64),
            ]),
        );
        trace.insert(
            1.0,
            std::collections::HashMap::from([
                ("x".to_string(), 0.2_f64),
                ("y".to_string(), 2.0_f64),
            ]),
        );

        let metrics = formula.evaluate(&trace).expect("evaluate formula on trace");
        assert_eq!(metrics.len(), 2);
    });
}

#[test]
fn fails_when_variable_missing_from_trace() {
    // This test demonstrates the behaviour of Banquo/Banquo-parser when an STL
    // formula uses a variable that does not exist in the trace.
    //
    // The program (via `phi_observer`) currently unwraps the evaluation result,
    // so a missing variable would surface as a panic. Here we reproduce that
    // behaviour in a small, focused test.

    // Formula uses `pitch`, but the trace will only include `roll`.
    let formula: ParsedFormula =
        parse_formula("always 3.1*x <= 0.5*y and y <= 0.5").expect("parse well-formed formula");

    let mut trace: Trace<std::collections::HashMap<String, f64>> = Trace::new();
    // Intentionally omit `y` from the trace so evaluation fails with a
    // `Missing` variable error.
    trace.insert(
        0.0,
        std::collections::HashMap::from([("x".to_string(), 1.0_f64)]),
    );

    let result = formula.evaluate(&trace);
    assert!(result.is_err(), "expected evaluation to fail for missing variable");
}

#[test]
fn parses_production_stl_formulas_txt_with_same_logic() {
    // This test mirrors the parsing logic used by `PhysicalObserver::load_formulas_from_file`
    // against the real `stl_formulas.txt` that the fuzzer uses in production.

    // 1) Open the production STL formulas file from the crate root.
    let file = File::open("stl_formulas.txt")
        .expect("stl_formulas.txt should exist in the crate root for this test");
    let reader = BufReader::new(file);

    // 2) Apply the exact same parsing rules as `PhysicalObserver::load_formulas_from_file`:
    //    skip blank lines and comments, and panic if any formula cannot be parsed.
    let mut formulas: Vec<ParsedFormula> = Vec::new();
    for (idx, line_res) in reader.lines().enumerate() {
        let line_num = idx + 1;
        let line = line_res.unwrap_or_else(|err| {
            panic!("Error reading line {} from 'stl_formulas.txt': {}", line_num, err)
        });

        let s = line.trim().to_owned();
        if s.is_empty() || s.starts_with('#') {
            continue;
        }

        // Panic on parse error, mirroring the runtime behaviour in `PhysicalObserver`.
        let parsed = parse_formula(&s).unwrap_or_else(|e| {
            panic!(
                "Failed to parse STL formula on line {} of 'stl_formulas.txt': \"{}\"\nError: {}",
                line_num, s, e
            );
        });

        formulas.push(parsed);
    }

    // If we get here, no panic occurred and all formulas parsed successfully.
    // As an additional sanity check, the current production file should define 5 formulas.
    assert_eq!(
        formulas.len(),
        5,
        "Expected 5 formulas to be parsed from stl_formulas.txt"
    );
}


