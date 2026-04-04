use serde::{Deserialize, Serialize};
use serde_json::json;
use std::collections::HashMap;
use std::fs;
use std::env;
use serde_json::Value;
use crate::ParamInput;

#[derive(Serialize, Deserialize, Debug, Clone)]
struct ChatMessage {
    role: String,
    content: String,
}

#[derive(Serialize)]
struct ChatRequest {
    model: String,
    messages: Vec<ChatMessage>,
}

#[derive(Deserialize, Debug)]
struct Choice {
    message: ChatMessage,
}

#[derive(Deserialize, Debug)]
struct ChatResponse {
    choices: Vec<Choice>,
}


pub fn ask_openai(api_key: &str, prompt: &str) -> Result<String, Box<dyn std::error::Error>> {
    let client = reqwest::blocking::Client::new();

    let openai_url = env::var("OPENAI_URL")
        .unwrap_or_else(|_| "https://api.openai.com/v1/chat/completions".to_string());

    let body = ChatRequest {
        model: "gpt-4.1-mini".to_string(),
        messages: vec![ChatMessage {
            role: "user".to_string(),
            content: prompt.to_string(),
        }],
    };

    let res = client
        .post(&openai_url)
        .header("Authorization", format!("Bearer {}", api_key))
        .header("Content-Type", "application/json")
        .json(&body)
        .send()?;

    let parsed: ChatResponse = res.json()?;

    let reply = parsed
        .choices
        .get(0)
        .ok_or("No response from OpenAI")?
        .message
        .content
        .clone();

    Ok(reply)
}

pub fn get_parameter_block_text(file_path: &str, target_param: &str) -> Option<String> {
    let text = fs::read_to_string(file_path)
        .expect("Failed to read file");

    let key = format!("\"{}\"", target_param);

    let start = text.find(&key)?;

    // Find first '{' after the parameter name
    let brace_start = text[start..].find('{')? + start;

    // Walk forward and match nested braces
    let mut depth = 0usize;

    for (i, ch) in text[brace_start..].char_indices() {
        match ch {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    let end = brace_start + i + 1;
                    return Some(text[brace_start..end].to_string());
                }
            }
            _ => {}
        }
    }

    None // unmatched braces
}

pub fn get_parameter_names(
    file_path: &str,
) -> (HashMap<String, Vec<String>>, Vec<String>) {
    let text = fs::read_to_string(file_path)
        .expect("Failed to read file");

    let data: Value = serde_json::from_str(&text)
        .expect("Invalid JSON");

    let mut param_names: HashMap<String, Vec<String>> = HashMap::new();

    // Collect top-level keys (group names)
    let top_level_keys: Vec<String> = data
        .as_object()
        .map(|m| m.keys().cloned().collect())
        .unwrap_or_default();

    // Iterate groups
    if let Some(groups) = data.as_object() {
        for (group_name, group_params) in groups {
            // Skip SIM_ groups
            if group_name.starts_with("SIM_") {
                continue;
            }

            if let Some(params) = group_params.as_object() {
                for (param_name, param_dict) in params {
                    // Heuristic: must be an object with "Description"
                    if param_dict.is_object()
                        && param_dict.get("Description").is_some()
                        && !param_name.starts_with("SIM_")
                    {
                        param_names
                            .entry(group_name.clone())
                            .or_default()
                            .push(param_name.clone());
                    }
                }
            }
        }
    }

    (param_names, top_level_keys)
}

// pub fn generate_parameter_shim_prompt(
//     parameter_names: &[String],
//     flight_controller: &str,
//     vehicle_type: &str,
// ) -> String {
//     let parameter_list = parameter_names
//         .iter()
//         .map(|name| format!("{}", name))
//         .collect::<Vec<String>>()
//         .join("\n");
//     let parameter_shim_prompt = format!(
//     r#"
// You are an assistant that maps flight-control parameters to structured domains.

// I will provide you with:
// 1) A list of human readable parameter descriptions (strings).
// 2) A flight controller type (e.g., ArduPilot, PX4, etc.).
// 3) A vehicle type (e.g., rover, quadcopter, plane, submarine, etc.).

// Your task is to return, for EACH parameter, exactly one line in the following strict format:

// {{<parameter_name>, <categorical | continuous>, <range OR list>}}

// where the parameter_name is the parameter from the flight controller that best matches the provided description.
// Once you identify the parameter, determine if it is CONTINUOUS or CATEGORICAL based on its typical usage in the specified flight controller for the given vehicle type.

// Where:
// - If the parameter is CONTINUOUS, you must provide a numeric range of the form (min, max).
// - If the parameter is CATEGORICAL, you must provide a Python-style list of all plausible values, formatted like: [val1, val2, val3, ...]
// - Do NOT include any explanation, reasoning, or extra text outside of these formatted lines.
// - Do NOT add parameters that I did not provide.
// - Do NOT omit any parameter that I provided.

// Assume:
// - The flight controller is: {flight_controller}
// - The vehicle type is: {vehicle_type}

// If you are uncertain about a parameter’s domain, make the most reasonable assumption based on typical {flight_controller} behavior for a {vehicle_type}.

// Example of correct output format (DO NOT use these values unless they actually apply):

// User:
// Parameter list: "maximum steering rate, target cruise speed, and minimum turning radius"
// Flight controller: "ArduPilot"
// Vehicle type: "Rover"

// Output:
// {{ATC_STR_RAT_MAX, continuous, (0.0, 1000.0)}}
// {{CRUISE_SPEED, continuous, (50.0, 100.0)}}
// {{TURN_RADIUS, continuous, (0.0, 5.0)}}

// Below is the list of parameters to return outputs for:
// {parameter_list}
//     "#,
//         flight_controller = flight_controller,
//         vehicle_type = vehicle_type,
//         parameter_list = parameter_list,
//     );

//     parameter_shim_prompt
// }

pub fn ask_which_param(
    api_key: &str,
    high_level_names: &[String],
    parameter_list: &[String],
) -> Result<String, Box<dyn std::error::Error>> {
    let parameter_list = parameter_list
        .iter()
        .map(|name| format!("- {}", name))
        .collect::<Vec<String>>()
        .join("\n");
    let prompt = format!(
    r#"
You are an assistant that maps high-level, human-readable parameter descriptions
to ArduPilot top-level parameter prefixes (e.g., "ATC_", "TURN_", etc.).

I will provide you with:
1) A list of available TOP-LEVEL PARAMETER PREFIXES (these are the ONLY valid choices).
2) A list of HUMAN-READABLE HIGH-LEVEL DESCRIPTIONS that need to be mapped.

YOUR TASK:
For EACH human-readable description I provide, you must select exactly ONE matching
top-level prefix from the list of allowed options.

STRICT RULES:
- You may ONLY use prefixes that appear in the provided list.
- You must choose EXACTLY ONE prefix per description.
- You must NOT invent new prefixes.
- You must NOT omit any description.
- If two prefixes seem plausible, pick the best single match.
- Do NOT include explanations or reasoning.

OUTPUT FORMAT (one line per mapping):

"Human Description":"EXACT_PREFIX_FROM_LIST"

------------------------------------------------------------
ALLOWED PARAMETERS (WHITELIST):
{parameter_list}

DESCRIPTIONS TO MAP:
{human_descriptions}
------------------------------------------------------------
    "#,
        parameter_list = parameter_list,
        human_descriptions = high_level_names.join("\n"),
    );
    let response = ask_openai(api_key, &prompt)?;
    Ok(response)
}

pub fn ask_for_range_or_values(
    api_key: &str,
    param_name: &str,
    block: &str,
) -> Result<String, Box<dyn std::error::Error>> {
    let prompt = format!(
    r#"
You are an assistant that classifies a single ArduPilot parameter based ONLY on the
information provided below. Do NOT rely on outside knowledge of ArduPilot.

You will be given:
1) A PARAMETER NAME
2) The exact JSON block associated with that parameter (as raw text)

YOUR TASK:
Return exactly ONE line in the following strict format (no explanations, no prose):

{{<parameter_name>, <continuous | categorical>, <range OR list>}}

RULES:
- You must use the EXACT parameter name provided.
- If the parameter is numeric or has a numeric "Range", classify it as: continuous
  and output a range in the form (min, max).
- If the parameter is clearly an enumerated mode, boolean, or choice, classify it as: categorical
  and output a Python-style list of all possible values, like: [A, B, C].
- If the JSON block contains "Range", you should use that information.
- If the JSON block contains textual hints like "set to 0 to disable", treat 0 as part of the range.
- Do NOT invent new parameter names.
- Do NOT invent values that are not supported by the text.

------------------------------------------------------------
PARAMETER NAME:
{param_name}

JSON BLOCK:
{block}
------------------------------------------------------------
    "#,
        param_name = param_name,
        block = block,
    );

    let response = ask_openai(api_key, &prompt)?;
    Ok(response)
}

pub fn param_discern_loop(
    api_key: &str,
    flight_controller: &str,
    vehicle_type: &str,
    high_level_names: &[String],
) -> Result<String, Box<dyn std::error::Error>> {
    // first get all parameter names
    // in future check the flight controller and vehicle type to pick the right file
    let param_hash = get_parameter_names("parameter_lists/rover-4.6.json");
    let top_level_keys = param_hash.1;
    let param_names = param_hash.0.values().flatten().cloned().collect::<Vec<String>>();
    let param_mapping_str = ask_which_param(
        api_key,
        high_level_names,
        &param_names,
    )?;

    // a variable to store all the strings put together
    let mut full_response = String::new();

    for line in param_mapping_str.lines() {
        let parts: Vec<&str> = line.split(':').collect();
        if parts.len() != 2 {
            continue;
        }
        let high_level_name = parts[0].trim().trim_matches('"');
        let param_name = parts[1].trim().trim_matches('"');
        let param_block = match get_parameter_block_text("parameter_lists/rover-4.6.json", param_name) {
            Some(block) => block,
            None => {
                eprintln!("Warning: Could not find parameter block for {}", param_name);
                continue;
            }
        };
        let range_or_values = ask_for_range_or_values(
            api_key,
            param_name,
            &param_block,
        )?;
        // add to response
        full_response.push_str(range_or_values.as_str());
        full_response.push('\n');
    }


    Ok(full_response)
}

pub fn read_high_level_params_from_file(
    file_path: &str,
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let content = std::fs::read_to_string(file_path)?;
    let params: Vec<String> = content
        .lines()
        .map(|line| line.trim().to_string())
        .filter(|line| !line.is_empty())
        .collect();
    Ok(params)
}

pub fn get_parameter_shim_from_openai(
    api_key: &str,
    flight_controller: &str,
    vehicle_type: &str,
) -> Result<String, Box<dyn std::error::Error>> {
    // get high level params from file
    let parameter_names = read_high_level_params_from_file("param_shim_descriptions.txt")?;
    // let prompt = generate_parameter_shim_prompt(
    //     &parameter_names,
    //     flight_controller,
    //     vehicle_type,
    // );
    // let response = ask_openai(api_key, &prompt)?;
    let response = param_discern_loop(
        api_key,
        flight_controller,
        vehicle_type,
        &parameter_names,
    )?;

    Ok(response)
}

/**
 * Parses the response from OpenAI into a vector of ParamInput structs.
 * let mut generated_param_input: Vec<ParamInput> = param_shim::parse_param_shim_response(&api_key, &response)
        .expect("Failed to parse parameter shim response");
 * skip any non-continuous parameters and make range tuples into f64
 * increment always 0.1 for each continuous parameter
 */
pub fn parse_param_shim_response(
    api_key: &str,
    response: &str,
) -> Result<Vec<ParamInput>, Box<dyn std::error::Error>> {
    let mut param_info_vec: Vec<ParamInput> = Vec::new();

    for line in response.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }

        if !line.starts_with('{') || !line.ends_with('}') {
            eprintln!("Warning: Invalid line format: {}", line);
            continue;
        }

        let content = &line[1..line.len() - 1]; // remove { and }

        // --- NEW: split only on the first two commas ---
        let mut parts = Vec::new();
        let mut start = 0;
        let mut depth = 0; // track parentheses depth

        for (i, ch) in content.char_indices() {
            match ch {
                '(' => depth += 1,
                ')' => depth -= 1,
                ',' if depth == 0 => {
                    parts.push(content[start..i].trim());
                    start = i + 1;
                }
                _ => {}
            }
        }
        // push the final segment
        parts.push(content[start..].trim());

        if parts.len() != 3 {
            eprintln!("Warning: Invalid number of parts in line: {}", line);
            continue;
        }

        let param_name = parts[0].to_string();
        let param_type = parts[1];
        let range_or_values = parts[2];

        if param_type != "continuous" {
            continue; // skip categorical for now
        }

        if !range_or_values.starts_with('(') || !range_or_values.ends_with(')') {
            eprintln!("Warning: Invalid range format in line: {}", line);
            continue;
        }

        let range_content = &range_or_values[1..range_or_values.len() - 1];
        let range_parts: Vec<&str> = range_content
            .split(',')
            .map(|s| s.trim())
            .collect();

        if range_parts.len() != 2 {
            eprintln!("Warning: Invalid range parts in line: {}", line);
            continue;
        }

        let min: f64 = range_parts[0].parse()?;
        let max: f64 = range_parts[1].parse()?;

        param_info_vec.push(ParamInput::new_float(
            &param_name,
            (min, max),
            0.1,
        ));
    }

    Ok(param_info_vec)
}
