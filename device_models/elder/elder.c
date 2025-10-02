#include <device.h>
#include <hw.h>
#include <utils.h>
#include "device_config.h"
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>

// Global hardware handle, if we use hardware.
typedef struct {
    DeviceModels* config;  // Embed original config struct

    void *handle;           // dlopen handle

    // function pointers to device callbacks
	DeviceModel model;
} device_model_t;

device_model_t devices[MAX_DEVICES];
DeviceModels configs[MAX_DEVICES];

static int device_count = 0;

static uint64_t elder_read(void *opaque, hwaddr address, unsigned size) {
	for (int i = 0; i < device_count; i++) {
		Range ranges[10];
		utils_parse_ranges(devices[i].config->range_count,devices[i].config->ranges, ranges);
		for (int j=0; j<devices[i].config->range_count; j++) {
			if (address >= ranges[j].start && address < ranges[j].end) {
				// Good case, my scroll worked
				return devices[i].model.read(NULL, address, size);
			}
		}
    }
	utils_die("Unable to map any address to any address of elder read function");
	return 0; // never reached, but silences compiler

}

static void elder_write(void *opaque, hwaddr address, uint64_t value, unsigned size) {
	for (int i = 0; i < device_count; i++) {
		Range ranges[10];
		utils_parse_ranges(devices[i].config->range_count,devices[i].config->ranges, ranges);
		for (int j=0; j<devices[i].config->range_count; j++) {
			if (address >= ranges[j].start && address < ranges[j].end) {
				// Good case, my scroll worked
				devices[i].model.write(NULL, address, value, size);
				return;
			}
		}
    }
}

static int elder_init(ConfigSection* model_info);
// The public definition of the elder device model
DeviceModel elder_model_def = {
    .name = "elder",
    .read = elder_read,
    .write = elder_write,
    .init = elder_init,
};

static int elder_init(ConfigSection* model_info) {
    //Find the overall ranges for all the devices registered as passthrough
    Range ranges[10];
	device_count = model_info->device_count;
    utils_parse_ranges(model_info->overall_range_count,model_info->overall_ranges, ranges);

	for (int i = 0; i < model_info->overall_range_count; i++) {
        dev_register_device_model(ranges[i].start, ranges[i].end, &elder_model_def);
    }

	for (int i=0; i < model_info->device_count; i++) {
		devices[i].config = &model_info->devices[i];
		printf("Loading device [%s] from %s\n", devices[i].config->name, devices[i].config->scroll_path);

	    devices[i].handle = dlopen(devices[i].config->scroll_path, RTLD_NOW);
	    if (!devices[i].handle) {
	        fprintf(stderr, "dlopen failed: %s\n", dlerror());
	        utils_die("dlopen error");
	    }

	    char symbol[256];
	    snprintf(symbol, sizeof(symbol), "%s_read", devices[i].config->name);
	    devices[i].model.read = (DeviceReadFunc)dlsym(devices[i].handle, symbol);
	    if (!devices[i].model.read) utils_die("Missing read");

	    snprintf(symbol, sizeof(symbol), "%s_write", devices[i].config->name);
	    devices[i].model.write = (DeviceWriteFunc)dlsym(devices[i].handle, symbol);
	    if (!devices[i].model.write) utils_die("Missing write");

	    snprintf(symbol, sizeof(symbol), "%s_init", devices[i].config->name);
	    devices[i].model.init = (DeviceInit)dlsym(devices[i].handle, symbol);
	    if (!devices[i].model.init) utils_die("Missing init");

		devices[i].model.init(model_info);
	}
	return 0;
}
