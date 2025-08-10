#include <device.h>
#include <hw.h>
#include <utils.h>
#include "device_config.h"
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>

// Global hardware handle, if we use hardware.
static hw_t * hw;
typedef struct {
    device_config_t * config;  // Embed original config struct

    void *handle;           // dlopen handle

    // function pointers to device callbacks
	DeviceModel model;
} device_model_t;

device_model_t devices[MAX_DEVICES];
device_config_t configs[MAX_DEVICES];

static int device_count = 0;

static uint64_t elder_read(void *opaque, hwaddr address, unsigned size) {
	uint64_t return_val;
	uint32_t value_read;

	for (int i = 0; i < device_count; i++) {
        if (address >= devices[i].config->base && address < devices[i].config->base + devices[i].config->size) {
			// Good case, my scroll worked
            return devices[i].model.read(NULL, address, size);
        }
    }

	int status = hw_read32(hw, address, &value_read);
	if (status != 0) {
			utils_die("HW Read Failed");
	}
	return_val = value_read;
	return return_val;
}

static void elder_write(void *opaque, hwaddr address, uint64_t value, unsigned size) {
	for (int i = 0; i < device_count; i++) {
        if (address >= devices[i].config->base && address < devices[i].config->base + devices[i].config->size) {
			//Good case, my scroll worked
            devices[i].model.write(NULL, address, value, size);
            return;
        }
    }

	int status = hw_write32(hw, address, (uint32_t)value);
	if (status != 0) {
			utils_die("HW Write Failed");
	}
}



static int elder_init(char * argument) {
	char *sep = strchr(argument, '@');
    if (!sep) {
        printf("No '&' found in input\n");
        return 1;
    }

    // Replace '&' with '\0' to split string in-place
    *sep = '\0';

    char *hw_arg = argument;
    char *scroll_arg = sep + 1;

    printf("Will use Backend: %s\n", hw_arg);
    printf("and the scroll: %s\n", scroll_arg);

	device_count = parse_config(scroll_arg, configs, MAX_DEVICES);
    if (device_count < 0) {
        fprintf(stderr, "Failed to parse config file: %s\n", argument);
        utils_die("Config file parsing failed.");
        return -1; // Just in case utils_die doesn't exit immediately
    }

	for (int i = 0; i < device_count; i++) {
	    devices[i].config = &configs[i];
		printf("Loading device [%s] from %s\n", devices[i].config->section, devices[i].config->libpath);

	    devices[i].handle = dlopen(devices[i].config->libpath, RTLD_NOW);
	    if (!devices[i].handle) {
	        fprintf(stderr, "dlopen failed: %s\n", dlerror());
	        utils_die("dlopen error");
	    }

	    char symbol[256];
	    snprintf(symbol, sizeof(symbol), "%s_read", devices[i].config->section);
	    devices[i].model.read = (DeviceReadFunc)dlsym(devices[i].handle, symbol);
	    if (!devices[i].model.read) utils_die("Missing read");

	    snprintf(symbol, sizeof(symbol), "%s_write", devices[i].config->section);
	    devices[i].model.write = (DeviceWriteFunc)dlsym(devices[i].handle, symbol);
	    if (!devices[i].model.write) utils_die("Missing write");

	    snprintf(symbol, sizeof(symbol), "%s_init", devices[i].config->section);
	    devices[i].model.init = (DeviceInit)dlsym(devices[i].handle, symbol);
	    if (!devices[i].model.init) utils_die("Missing init");

	    devices[i].model.init(scroll_arg);
	}

	// Connect to hardware for what we don't know 
	hw = hw_connect(hw_arg, NULL, 0);

	if (!hw) {
			utils_die("HW connection failed.");
	}

	return 0;
}

// The public definition of the elder device model
DeviceModel elder_model_def = {
    .name = "elder",
    .read = elder_read,
    .write = elder_write,
	.init = elder_init,
};

