/* Auto-generated C Header for FMU: RealisticTempSensor */
#ifndef REALISTICTEMPSENSOR_HARNESS_H
#define REALISTICTEMPSENSOR_HARNESS_H

#define FMU_GUID "{00684eaa-c790-4b41-afbe-be5e747b4269}"
#define MODEL_IDENTIFIER "RealisticTempSensor"

/* --- PARAMETERS --- */
#define REALISTICTEMPSENSOR_NUM_PARAMETERS 7
#define VREF_PARAM_C 7 // Type: Real
#define VREF_PARAM_R 8 // Type: Real
#define VREF_PARAM_ADCRESOLUTION 9 // Type: Real
#define VREF_PARAM_AMBIENTTEMP 10 // Type: Real
#define VREF_PARAM_MAXTEMP 11 // Type: Real
#define VREF_PARAM_MINTEMP 12 // Type: Real
#define VREF_PARAM_SELFHEATINGPOWER 13 // Type: Real

/* --- SENSORS (Inputs) --- */
#define REALISTICTEMPSENSOR_NUM_INPUTS 1
#define VREF_IN_HEATSOURCE 4 // Type: Real

/* --- ACTUATORS (Outputs) --- */
#define REALISTICTEMPSENSOR_NUM_OUTPUTS 1
#define VREF_OUT_SENSEDTEMP 6 // Type: Real

/* ValueReference Arrays for Batch Processing */
static const fmi2ValueReference REALISTICTEMPSENSOR_PARAMETER_REFS[7] = {
    7, 8, 9, 10, 11, 12, 13
};

static const fmi2ValueReference REALISTICTEMPSENSOR_INPUT_REFS[1] = {
    4
};

static const fmi2ValueReference REALISTICTEMPSENSOR_OUTPUT_REFS[1] = {
    6
};

#endif // REALISTICTEMPSENSOR_HARNESS_H
