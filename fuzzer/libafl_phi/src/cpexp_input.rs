use libafl::inputs::Input;
use std::hash::Hash;
use serde::{Deserialize, Serialize};
use ordered_float::OrderedFloat;
use libafl::inputs::bytes;

#[derive(Serialize, Deserialize, Debug, Clone, Hash)]
pub struct ParamInput {
    name: String,

    // All Ardu_X parameters are floats in a range or categorical values
    is_float: bool,

    // If the parameter is float, specify the range and increment
    #[serde(skip)]
    range: Option<(OrderedFloat<f64>, OrderedFloat<f64>)>,
    #[serde(skip)]
    increment: Option<OrderedFloat<f64>>,

    // If the parameter is categorical (is_float == false), specify the vector of options
    // Each option in the vector can either be a float or an int.
    // If float -> truncate == true, If int -> truncate == false
    #[serde(skip)]
    options: Option<Vec<OrderedFloat<f64>>>,
    truncate: bool,
}

#[derive(Serialize, Deserialize, Debug, Clone, Hash)]
pub struct EnvInput {
    name: String,

    // "Slider" to adjust environment inputs
    #[serde(skip)]
    range: (OrderedFloat<f64>, OrderedFloat<f64>),

    // Some environments inputs are categorical, e.g. terrain
    // In this case, range represents the indices of the categorical options
    // that will be truncated to integers
    categorical: bool,
}

#[derive(Serialize, Deserialize, Debug, Clone, Hash)]
pub struct CPExpInput {
    param_input: Vec<ParamInput>,
    env_input: Vec<EnvInput>,
}

impl Input for CPExpInput {

    fn from_file<P>(path: P) -> Result<Self, libafl::Error>
        where
            P: AsRef<std::path::Path>, {

        todo!()

    }

    fn to_file<P>(&self, path: P) -> Result<(), libafl::Error>
        where
            P: AsRef<std::path::Path>, {

        todo!()

    }

    fn generate_name(&self, _id: Option<libafl::corpus::CorpusId>) -> String {

        todo!()

    }

}

impl EnvInput {

    pub fn new(name: &str, range: (f64, f64), categorical: bool) -> Self {

        Self {
            name: String::from(name),
            range: (OrderedFloat(range.0), OrderedFloat(range.1)),
            categorical,
        }

    }

}

impl ParamInput {

    pub fn new_float(name: &str, range: (f64, f64), increment: f64) -> Self {

        Self {
            name: String::from(name),
            is_float: true,
            range: Some((OrderedFloat(range.0), OrderedFloat(range.1))),
            increment: Some(OrderedFloat(increment)),
            options: None,
            truncate: false,
        }

    }

    pub fn new_categorical(name: &str, options: Vec<f64>, truncate: bool) -> Self {

        let ordered_options: Vec<OrderedFloat<f64>> = options.into_iter().map(|x| OrderedFloat(x)).collect();

        Self {
            name: String::from(name),
            is_float: false,
            range: None,
            increment: None,
            options: Some(ordered_options),
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

}