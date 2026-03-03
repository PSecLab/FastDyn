#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <fmi2Functions.h>

#include <fmi2FunctionTypes.h>

// --- 1. Include the Auto-Generated Header ---
#include "fmu_header.h" 

#if 01

// --- Function Pointer Types ---
typedef fmi2Component (*fmi2Instantiate_ft)(fmi2String, fmi2Type, fmi2String, fmi2String, const fmi2CallbackFunctions*, fmi2Boolean, fmi2Boolean);
typedef fmi2Status    (*fmi2SetupExperiment_ft)(fmi2Component, fmi2Boolean, fmi2Real, fmi2Real, fmi2Boolean, fmi2Real);
typedef fmi2Status    (*fmi2EnterInitializationMode_ft)(fmi2Component);
typedef fmi2Status    (*fmi2ExitInitializationMode_ft)(fmi2Component);
typedef fmi2Status    (*fmi2DoStep_ft)(fmi2Component, fmi2Real, fmi2Real, fmi2Boolean);
typedef fmi2Status    (*fmi2GetReal_ft)(fmi2Component, const fmi2ValueReference[], size_t, fmi2Real[]);
typedef fmi2Status    (*fmi2SetReal_ft)(fmi2Component, const fmi2ValueReference[], size_t, const fmi2Real[]);
typedef fmi2Status    (*fmi2SetInteger_ft)(fmi2Component, const fmi2ValueReference[], size_t, const fmi2Integer[]);
typedef fmi2Status    (*fmi2SetBoolean_ft)(fmi2Component, const fmi2ValueReference[], size_t, const fmi2Boolean[]);
typedef void          (*fmi2FreeInstance_ft)(fmi2Component);
#endif 

// --- FMI Callback Functions ---
void cb_log(fmi2ComponentEnvironment env, fmi2String instanceName, fmi2Status status, fmi2String category, fmi2String message, ...) {
    printf("[FMI LOG][%s] %s\n", instanceName, message);
}

// --- Harness Structure ---
typedef struct {
    void* handle;
    fmi2Component instance;
    fmi2Instantiate_ft instantiate;
    fmi2SetupExperiment_ft setup;
    fmi2EnterInitializationMode_ft enterInit;
    fmi2ExitInitializationMode_ft exitInit;
    fmi2DoStep_ft doStep;
    fmi2GetReal_ft getReal;
    fmi2SetReal_ft setReal;
    fmi2SetInteger_ft setInteger;
    fmi2SetBoolean_ft setBoolean;
    fmi2FreeInstance_ft freeInstance;
} FMU;

const char* resource_uri = "file:///";

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <path_to_so_file>\n", argv[0]);
        return 1;
    }

    FMU fmu;
    fmu.handle = dlopen(argv[1], RTLD_LAZY);
    if (!fmu.handle) {
        fprintf(stderr, "Error loading .so: %s\n", dlerror());
        return 1;
    }

    // 1. Map Symbols (Added Integer and Boolean setters for seeds)
    fmu.instantiate  = (fmi2Instantiate_ft)dlsym(fmu.handle, "fmi2Instantiate");
    fmu.setup        = (fmi2SetupExperiment_ft)dlsym(fmu.handle, "fmi2SetupExperiment");
    fmu.enterInit    = (fmi2EnterInitializationMode_ft)dlsym(fmu.handle, "fmi2EnterInitializationMode");
    fmu.exitInit     = (fmi2ExitInitializationMode_ft)dlsym(fmu.handle, "fmi2ExitInitializationMode");
    fmu.doStep       = (fmi2DoStep_ft)dlsym(fmu.handle, "fmi2DoStep");
    fmu.getReal      = (fmi2GetReal_ft)dlsym(fmu.handle, "fmi2GetReal");
    fmu.setReal      = (fmi2SetReal_ft)dlsym(fmu.handle, "fmi2SetReal");
    fmu.setInteger   = (fmi2SetInteger_ft)dlsym(fmu.handle, "fmi2SetInteger");
    fmu.setBoolean   = (fmi2SetBoolean_ft)dlsym(fmu.handle, "fmi2SetBoolean");
    fmu.freeInstance = (fmi2FreeInstance_ft)dlsym(fmu.handle, "fmi2FreeInstance");

    fmi2CallbackFunctions callbacks = {cb_log, calloc, free, NULL, NULL};

    // 2. Instantiate using generated macros
    fmu.instance = fmu.instantiate(MODEL_IDENTIFIER, fmi2CoSimulation, 
                                   FMU_GUID, resource_uri, &callbacks, fmi2False, fmi2False);

    if (!fmu.instance) {
        printf("Failed to instantiate FMU\n");
        return 1;
    }

    // 3. Initialization Sequence
    fmu.setup(fmu.instance, fmi2False, 0.0, 0.0, fmi2False, 0.0);
    fmu.enterInit(fmu.instance);

    // 3.5 Setup parameters & Determinism
    
    // Disable automatic OS entropy seeding
    fmi2ValueReference vr_auto_seed = VREF_PARAM_GLOBALSEED_USEAUTOMATICSEED;
    fmi2Boolean disable_auto = fmi2False;
    fmu.setBoolean(fmu.instance, &vr_auto_seed, 1, &disable_auto);

    // Set your deterministic master seed (this can be passed in via argv later)
    fmi2ValueReference vr_master_seed = VREF_PARAM_RNGSEED;
    fmi2Integer master_seed = 1337; 
    fmu.setInteger(fmu.instance, &vr_master_seed, 1, &master_seed);

    fmu.exitInit(fmu.instance);

    // 4. Simulation Setup using generated size macros
    fmi2Real current_outputs[ALTIMETER_NUM_OUTPUTS];
    fmi2Real current_inputs[ALTIMETER_NUM_INPUTS];

    // Seed initial input value (TrueAltitude in meters)
    current_inputs[0] = 50.0; 

    double h = 0.01; // Time step (100Hz matching the altimeter samplePeriod)
    double t = 0.0;

    // 5. Simulation Loop
    printf("Time\tTrueAltitude (In)\tSensedPressure (Out)\n");
    for (int i = 0; i < 200; i++) {
        // Write all inputs using the generated array
        fmu.setReal(fmu.instance, ALTIMETER_INPUT_REFS, ALTIMETER_NUM_INPUTS, current_inputs);

        // Advance the solver
        fmi2Status status = fmu.doStep(fmu.instance, t, h, fmi2True);
        if (status != fmi2OK) {
            printf("[WARNING] Solver failed at t=%.2f with FMI Status Code: %d\n", t, status);
        }

        // Read all outputs using the generated array
        fmu.getReal(fmu.instance, ALTIMETER_OUTPUT_REFS, ALTIMETER_NUM_OUTPUTS, current_outputs);
        
        // Log the current state
        printf("%.2f\t%.2f\t\t\t%.2f\n", t, current_inputs[0], current_outputs[0]);
                
        t += h;
    }

    // 6. Cleanup
    fmu.freeInstance(fmu.instance);
    dlclose(fmu.handle);

    return 0;
}
