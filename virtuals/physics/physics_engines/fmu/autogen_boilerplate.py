import fmpy
import sys

def generate_c_header(fmu_path):
    # Let fmpy parse the XML
    model_desc = fmpy.read_model_description(fmu_path)
    
    guid = model_desc.guid
    model_name = model_desc.modelName
    model_id = model_desc.coSimulation.modelIdentifier if model_desc.coSimulation else model_name
    
    # Categorize variables by causality
    inputs = [v for v in model_desc.modelVariables if v.causality == 'input']
    outputs = [v for v in model_desc.modelVariables if v.causality == 'output']
    parameters = [v for v in model_desc.modelVariables if v.causality == 'parameter']
    
    prefix = model_name.upper().replace(" ", "_")
    
    # Start building the C header string
    c_code = f"/* Auto-generated C Header for FMU: {model_name} */\n"
    c_code += f"#ifndef {prefix}_HARNESS_H\n"
    c_code += f"#define {prefix}_HARNESS_H\n\n"
     
    c_code += f'#define FMU_GUID "{guid}"\n'
    c_code += f'#define MODEL_IDENTIFIER "{model_id}"\n\n'
    
    # --- Generate Parameter Macros ---
    if parameters:
        c_code += f"/* --- PARAMETERS --- */\n"
        c_code += f"#define {prefix}_NUM_PARAMETERS {len(parameters)}\n"
        for var in parameters:
            var_name_clean = var.name.upper().replace(".", "_").replace(" ", "_")
            c_code += f"#define VREF_PARAM_{var_name_clean} {var.valueReference} // Type: {var.type}\n"
        c_code += "\n"

    # --- Generate Input (Sensor) Macros ---
    if inputs:
        c_code += f"/* --- SENSORS (Inputs) --- */\n"
        c_code += f"#define {prefix}_NUM_INPUTS {len(inputs)}\n"
        for var in inputs:
            var_name_clean = var.name.upper().replace(".", "_").replace(" ", "_")
            c_code += f"#define VREF_IN_{var_name_clean} {var.valueReference} // Type: {var.type}\n"
        c_code += "\n"
    
    # --- Generate Output (Actuator) Macros ---
    if outputs:
        c_code += f"/* --- ACTUATORS (Outputs) --- */\n"
        c_code += f"#define {prefix}_NUM_OUTPUTS {len(outputs)}\n"
        for var in outputs:
            var_name_clean = var.name.upper().replace(".", "_").replace(" ", "_")
            c_code += f"#define VREF_OUT_{var_name_clean} {var.valueReference} // Type: {var.type}\n"
        c_code += "\n"
    
    # --- Generate Arrays for easy looping in C ---
    c_code += f"/* ValueReference Arrays for Batch Processing */\n"
    
    if parameters:
        c_code += f"static const fmi2ValueReference {prefix}_PARAMETER_REFS[{len(parameters)}] = {{\n    "
        c_code += ", ".join([str(v.valueReference) for v in parameters])
        c_code += "\n};\n\n"

    if inputs:
        c_code += f"static const fmi2ValueReference {prefix}_INPUT_REFS[{len(inputs)}] = {{\n    "
        c_code += ", ".join([str(v.valueReference) for v in inputs])
        c_code += "\n};\n\n"
    
    if outputs:
        c_code += f"static const fmi2ValueReference {prefix}_OUTPUT_REFS[{len(outputs)}] = {{\n    "
        c_code += ", ".join([str(v.valueReference) for v in outputs])
        c_code += "\n};\n\n"

    c_code += f"#endif // {prefix}_HARNESS_H\n"
    
    return c_code

import argparse
import sys
# Make sure to include the generate_c_header function we wrote earlier above this

if __name__ == "__main__":
    # Set up the argument parser
    parser = argparse.ArgumentParser(description="Extract FMU interfaces and generate a C header harness.")
    parser.add_argument("fmu_file", help="Path to the input .fmu file")
    parser.add_argument("-o", "--output", default="fmu_harness.h", 
                        help="Output C header filename (default: fmu_harness.h)")
    
    args = parser.parse_args()

    try:
        # Pass the command-line argument to the generator
        c_header_content = generate_c_header(args.fmu_file)

        # Write to the specified .h file
        with open(args.output, "w") as f:
            f.write(c_header_content)

        print(f"Successfully generated {args.output}")
        print("\n--- Preview ---\n")
        print(c_header_content)

    except Exception as e:
        print(f"Failed to generate C code: {e}")
        sys.exit(1) # Return a non-zero exit code so build scripts know it failed
