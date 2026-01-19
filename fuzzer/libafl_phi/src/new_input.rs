use libafl::inputs::{BytesInput, Input};
use core::hash;
use std::{collections::HashMap, hash::{DefaultHasher, Hash, Hasher}, ptr::hash};
use serde::{Deserialize, Serialize};
use std::fs;
use scirs2_core::Array1;

/*
    ParamInput, EnvInput, and CPExpInput are NOT meant to be used by LibAFL.
    A CPExpInput object will be stored inside of the LibAFL state object, and
    will primarily be used to translate optimizer outputs into the actual
    parameter/environmental values.
*/

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct ParamInput {
    name: String,

    // All Ardu_X parameters are floats in a range or categorical values
    is_float: bool,

    // If the parameter is float, specify the range and increment
    range: Option<(f64, f64)>,
    increment: Option<f64>,

    // If the parameter is categorical (is_float == false), specify the vector of options
    // Each option in the vector can either be a float or an int.
    // If float -> truncate == true, If int -> truncate == false
    options: Option<Vec<f64>>,
    truncate: bool,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct EnvInput {
    name: String,

    // "Slider" to adjust environment inputs
    range: (f64, f64),

    // Some environments inputs are categorical, e.g. terrain
    // In this case, range represents the indices of the categorical options
    // that will be truncated to integers
    categorical: bool,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct CPExpInput {
    param_input: Vec<ParamInput>,
    env_input: Vec<EnvInput>,
}

impl EnvInput {

    pub fn new(name: &str, range: (f64, f64), categorical: bool) -> Self {

        Self {
            name: String::from(name),
            range: (range.0, range.1),
            categorical,
        }

    }

}

impl ParamInput {

    pub fn new_float(name: &str, range: (f64, f64), increment: f64) -> Self {

        Self {
            name: String::from(name),
            is_float: true,
            range: Some((range.0, range.1)),
            increment: Some(increment),
            options: None,
            truncate: false,
        }

    }

    pub fn new_categorical(name: &str, options: Vec<f64>, truncate: bool) -> Self {

        Self {
            name: String::from(name),
            is_float: false,
            range: None,
            increment: None,
            options: Some(options),
            truncate,
        }

    }

}

impl CPExpInput {

    pub fn new(param_input: Vec<ParamInput>, env_input: Vec<EnvInput>) -> Self {

        Self {
            param_input,
            env_input,
        }

    }

    pub fn get_param_input(&self) -> &Vec<ParamInput> {
        &self.param_input
    }

    pub fn get_env_input(&self) -> &Vec<EnvInput> {
        &self.env_input
    }

}

/*
    TargetInput is the actual input type that will be passed to the harness and
    live in the corpus. It represents the parameter inputs as raw bytes and 
    environmental inputs as a String of comma-separated f64 values for a single execution. 
*/

#[derive(Serialize, Deserialize, Debug, Clone, Hash)]
pub struct TargetInput {
    // Parameter inputs need to be raw bytes to be mutated by LibAFL
    param_bytes: BytesInput,
    // Need to use a String because f64's are not hashable
    env_config: String,
}

impl Input for TargetInput {

    fn from_file<P>(path: P) -> Result<Self, libafl::Error>
    where
        P: AsRef<std::path::Path>, 
    {
        let mut file = std::fs::File::open(path)?;
        let mut content = String::new();
        std::io::Read::read_to_string(&mut file, &mut content)?;
        
        let input = serde_json::from_str(&content)
            .map_err(|e| libafl::Error::serialize(e.to_string()))?;
        Ok(input)   
    }

    fn to_file<P>(&self, path: P) -> Result<(), libafl::Error>
    where
        P: AsRef<std::path::Path>, 
    {
        let json = serde_json::to_string(self)
            .map_err(|e| libafl::Error::serialize(e.to_string()))?;
        std::fs::write(path, json)?;
        Ok(())
    } 

    fn generate_name(&self, _id: Option<libafl::corpus::CorpusId>) -> String {
        let mut hasher = DefaultHasher::new();
        self.hash(&mut hasher);
        hasher.finish().to_string()
    }

}

impl TargetInput {

    pub fn new(param_bytes: BytesInput, env_config: String) -> Self {

        Self {
            param_bytes,
            env_config,
        }

    }

    pub fn opt_ask_to_target_input(ask_array: &Array1<f64>, input_library: &CPExpInput) -> Self {


        todo!();

    }

    pub fn get_param_bytes(&self) -> &BytesInput {
        &self.param_bytes
    }

    pub fn get_env_config(&self) -> &String {
        &self.env_config
    }

}