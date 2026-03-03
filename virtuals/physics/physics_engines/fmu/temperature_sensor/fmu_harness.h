/* Auto-generated C Header for FMU: TempSensor */
#ifndef TEMPSENSOR_HARNESS_H
#define TEMPSENSOR_HARNESS_H

#include <fmi2_user_functions.h> // Or your specific FMI header

#define FMU_GUID "{78901425-36a6-4e1c-bd27-89fead04a362}"
#define MODEL_IDENTIFIER "TempSensor"

/* --- SENSORS (Inputs) --- */
#define TEMPSENSOR_NUM_INPUTS 1
#define VREF_IN_HEATSOURCE 2 // Type: Real

/* --- ACTUATORS (Outputs) --- */
#define TEMPSENSOR_NUM_OUTPUTS 1
#define VREF_OUT_SENSEDTEMP 3 // Type: Real

/* ValueReference Arrays for Batch Processing */
static const fmi2ValueReference TEMPSENSOR_INPUT_REFS[1] = {
    2
};

static const fmi2ValueReference TEMPSENSOR_OUTPUT_REFS[1] = {
    3
};

#endif // TEMPSENSOR_HARNESS_H
