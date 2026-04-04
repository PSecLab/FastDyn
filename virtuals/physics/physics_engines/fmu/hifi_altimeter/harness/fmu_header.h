/* Auto-generated C Header for FMU: Altimeter */
#ifndef ALTIMETER_HARNESS_H
#define ALTIMETER_HARNESS_H

#define FMU_GUID "{5b373f65-0748-4d40-9e3d-3359d4e5f1bc}"
#define MODEL_IDENTIFIER "Altimeter"

/* --- PARAMETERS --- */
#define ALTIMETER_NUM_PARAMETERS 36
#define VREF_PARAM_L 17 // Type: Real
#define VREF_PARAM_M 18 // Type: Real
#define VREF_PARAM_P0 19 // Type: Real
#define VREF_PARAM_R 20 // Type: Real
#define VREF_PARAM_T0 21 // Type: Real
#define VREF_PARAM_ADCRESOLUTION 22 // Type: Real
#define VREF_PARAM_AWGN_MU 23 // Type: Real
#define VREF_PARAM_AWGN_STARTTIME 26 // Type: Real
#define VREF_PARAM_AWGN_Y_OFF 27 // Type: Real
#define VREF_PARAM_AWGNSIGMA 28 // Type: Real
#define VREF_PARAM_G 30 // Type: Real
#define VREF_PARAM_MAXPRESSURE 31 // Type: Real
#define VREF_PARAM_MINPRESSURE 32 // Type: Real
#define VREF_PARAM_PINKSOURCE_MU 33 // Type: Real
#define VREF_PARAM_PINKSOURCE_STARTTIME 36 // Type: Real
#define VREF_PARAM_PINKSOURCE_Y_OFF 37 // Type: Real
#define VREF_PARAM_PINKTIMECONST 38 // Type: Real
#define VREF_PARAM_SAMPLEPERIOD 39 // Type: Real
#define VREF_PARAM_SPIKEMAG 40 // Type: Real
#define VREF_PARAM_SPIKEPROB 41 // Type: Real
#define VREF_PARAM_SPIKETRIGGER_STARTTIME 43 // Type: Real
#define VREF_PARAM_SPIKETRIGGER_Y_MAX 44 // Type: Real
#define VREF_PARAM_SPIKETRIGGER_Y_MIN 45 // Type: Real
#define VREF_PARAM_SPIKETRIGGER_Y_OFF 46 // Type: Real
#define VREF_PARAM_AWGN_FIXEDLOCALSEED 13 // Type: Integer
#define VREF_PARAM_PINKSOURCE_FIXEDLOCALSEED 19 // Type: Integer
#define VREF_PARAM_RNGSEED 21 // Type: Integer
#define VREF_PARAM_SPIKETRIGGER_FIXEDLOCALSEED 23 // Type: Integer
#define VREF_PARAM_AWGN_USEAUTOMATICLOCALSEED 5 // Type: Boolean
#define VREF_PARAM_AWGN_USEGLOBALSEED 6 // Type: Boolean
#define VREF_PARAM_GLOBALSEED_ENABLENOISE 7 // Type: Boolean
#define VREF_PARAM_GLOBALSEED_USEAUTOMATICSEED 8 // Type: Boolean
#define VREF_PARAM_PINKSOURCE_USEAUTOMATICLOCALSEED 11 // Type: Boolean
#define VREF_PARAM_PINKSOURCE_USEGLOBALSEED 12 // Type: Boolean
#define VREF_PARAM_SPIKETRIGGER_USEAUTOMATICLOCALSEED 15 // Type: Boolean
#define VREF_PARAM_SPIKETRIGGER_USEGLOBALSEED 16 // Type: Boolean

/* --- SENSORS (Inputs) --- */
#define ALTIMETER_NUM_INPUTS 1
#define VREF_IN_TRUEALTITUDE 9 // Type: Real

/* --- ACTUATORS (Outputs) --- */
#define ALTIMETER_NUM_OUTPUTS 1
#define VREF_OUT_SENSEDPRESSURE 6 // Type: Real

/* ValueReference Arrays for Batch Processing */
static const fmi2ValueReference ALTIMETER_PARAMETER_REFS[36] = {
    17, 18, 19, 20, 21, 22, 23, 26, 27, 28, 30, 31, 32, 33, 36, 37, 38, 39, 40, 41, 43, 44, 45, 46, 13, 19, 21, 23, 5, 6, 7, 8, 11, 12, 15, 16
};

static const fmi2ValueReference ALTIMETER_INPUT_REFS[1] = {
    9
};

static const fmi2ValueReference ALTIMETER_OUTPUT_REFS[1] = {
    6
};

#endif // ALTIMETER_HARNESS_H
