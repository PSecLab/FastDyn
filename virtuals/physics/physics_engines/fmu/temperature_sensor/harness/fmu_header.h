/* Auto-generated C Header for FMU: TempSensor */
#ifndef TEMPSENSOR_HARNESS_H
#define TEMPSENSOR_HARNESS_H

#define FMU_GUID "{08531d53-aa35-41f5-b6a6-9075d8e01f4a}"
#define MODEL_IDENTIFIER "TempSensor"

/* --- PARAMETERS --- */
#define TEMPSENSOR_NUM_PARAMETERS 1
#define VREF_PARAM_AMBIENTTEMP 4 // Type: Real

/* --- SENSORS (Inputs) --- */
#define TEMPSENSOR_NUM_INPUTS 1
#define VREF_IN_HEATSOURCE 2 // Type: Real

/* --- ACTUATORS (Outputs) --- */
#define TEMPSENSOR_NUM_OUTPUTS 1
#define VREF_OUT_SENSEDTEMP 3 // Type: Real

/* ValueReference Arrays for Batch Processing */
static const fmi2ValueReference TEMPSENSOR_PARAMETER_REFS[1] = {
    4
};

static const fmi2ValueReference TEMPSENSOR_INPUT_REFS[1] = {
    2
};

static const fmi2ValueReference TEMPSENSOR_OUTPUT_REFS[1] = {
    3
};

#endif // TEMPSENSOR_HARNESS_H
