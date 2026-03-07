#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <fmi2Functions.h>
#include <fmi2FunctionTypes.h>
#include <virtuals.h>

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

FMU fmu;

void virtual_altimeter_setup(unsigned int cpu_index, void *udata) {

	//Hardcoded for now
	char * fmu_path = "/root/rooney/FastDyn/virtuals/physics/physics_engines/fmu/hifi_altimeter/output_folder/249.fmutmp/sources/build/Altimeter.so";

    fmu.handle = dlopen(fmu_path, RTLD_LAZY);
    if (!fmu.handle) {
        fprintf(stderr, "Error loading .so: %s\n", dlerror());
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
}

double qemu_to_fmu_time(int64_t qemu_ns)
{
    return (double)qemu_ns / 1e9;
}


double t = 0.0;           // absolute FMU time
int64_t last_qemu_ns = 0; // last QEMU virtual time in ns

void virtual_altimeter_get(unsigned int cpu_index, void *udata) {
	// 4. Simulation Setup using generated size macros
    fmi2Real current_outputs[ALTIMETER_NUM_OUTPUTS];
    fmi2Real current_inputs[ALTIMETER_NUM_INPUTS];

    // Seed initial input value (TrueAltitude in meters)
    current_inputs[0] = 50.0;


	int64_t now_qemu_ns = qemu_plugin_get_virtual_timer(); // QEMU time in ns
	double now_fmu_sec = qemu_to_fmu_time(now_qemu_ns);

	//Delta = step size for FMU
	double h = 0.01; // fallback fixed step
	if (last_qemu_ns != 0) {  // after first step
	    h = now_fmu_sec - t;  // delta in seconds
	}


    // 5. Simulation Loop
   // printf("Time\tTrueAltitude (In)\tSensedPressure (Out)\n");
    // Write all inputs using the generated array
    fmu.setReal(fmu.instance, ALTIMETER_INPUT_REFS, ALTIMETER_NUM_INPUTS, current_inputs);

    // Advance the solver
    fmi2Status status = fmu.doStep(fmu.instance, t, h, fmi2True);
    if (status != fmi2OK) {
        printf("[WARNING] Solver failed at t=%.2f with FMI Status Code: %d\n", t, status);
    }

    // Read all outputs using the generated array
    fmu.getReal(fmu.instance, ALTIMETER_OUTPUT_REFS, ALTIMETER_NUM_OUTPUTS, current_outputs);

	//TODO: update qemu state if required

	// Update t and last QEMU time
    t += h;
    last_qemu_ns = now_qemu_ns;

}


void virtual_altimeter_teardown(unsigned int cpu_index, void *udata) {

	fmu.freeInstance(fmu.instance);
	dlclose(fmu.handle);
}

int virtual_altimeter_init(int argc, char **argv) {
		virtual_register("altimeter_setup", virtual_altimeter_setup);
		virtual_register("altimeter_get", virtual_altimeter_get);
		virtual_register("altimeter_destroy", virtual_altimeter_teardown);

		return 0;
}
