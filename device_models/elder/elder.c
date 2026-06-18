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

static uint64_t elder_read(void *opaque, hwaddr address, unsigned size, uint64_t pc) {
	for (int i = 0; i < device_count; i++) {
		Range ranges[MAX_DEVICES];
		utils_parse_ranges(devices[i].config->range_count,devices[i].config->ranges, ranges);
		for (int j=0; j<devices[i].config->range_count; j++) {
			if (address >= ranges[j].start && address < ranges[j].end) {
				// Good case, my scroll worked
				return devices[i].model.read(devices[i].model.opaque, address, size, pc);
			}
		}
    }
	char err_buf[128];
	snprintf(err_buf, sizeof(err_buf),
             "Unable to map any address to any address of elder read function (address: 0x%lx, pc: 0x%lx)",
             address, pc);
	utils_die(err_buf);
	return 0; // never reached, but silences compiler

}

static void elder_write(void *opaque, hwaddr address, uint64_t value, unsigned size, uint64_t pc) {
	for (int i = 0; i < device_count; i++) {
		Range ranges[MAX_DEVICES];
		utils_parse_ranges(devices[i].config->range_count,devices[i].config->ranges, ranges);
		for (int j=0; j<devices[i].config->range_count; j++) {
			if (address >= ranges[j].start && address < ranges[j].end) {
				// Good case, my scroll worked
				devices[i].model.write(devices[i].model.opaque, address, value, size, pc);
				return;
			}
		}
    }
}

static int elder_serve(int number) {
	for (int i = 0; i < device_count; i++) {
		if (devices[i].model.serve) {
			devices[i].model.serve(number);
		}
	}
    return 0;
}

static int elder_interrupt(int number) {
	for (int i = 0; i < device_count; i++) {
		if (devices[i].model.interrupt) {
			devices[i].model.interrupt(number);
		}
	}
    return 0;
}

static void* elder_init(ConfigSection* model_info);
// The public definition of the elder device model
DeviceModel elder_model_def = {
    .name = "elder",
    .read = elder_read,
    .write = elder_write,
    .init = elder_init,
    .serve = elder_serve,
    .interrupt = elder_interrupt,
};

static void* elder_init(ConfigSection* model_info) {
    //Find the overall ranges for all the devices registered as elder
    Range ranges[MAX_DEVICES];
	device_count = model_info->device_count;
    utils_parse_ranges(model_info->overall_range_count,model_info->overall_ranges, ranges);

	for (int i = 0; i < model_info->overall_range_count; i++) {
        dev_register_device_model(ranges[i].start, ranges[i].end, &elder_model_def);
    }

	for (int i=0; i < model_info->device_count; i++) {
		devices[i].config = &model_info->devices[i];
		devices[i].model.name = devices[i].config->name;
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

        // Optional callbacks for IRQs
	    snprintf(symbol, sizeof(symbol), "%s_serve", devices[i].config->name);
	    devices[i].model.serve = (DeviceIRQFunc)dlsym(devices[i].handle, symbol);

	    snprintf(symbol, sizeof(symbol), "%s_interrupt", devices[i].config->name);
	    devices[i].model.interrupt = (DeviceIRQFunc)dlsym(devices[i].handle, symbol);

		/* Call init and store the returned state pointer as opaque. */
		devices[i].model.opaque = devices[i].model.init(model_info);

        // Register IRQs for this device based on config
        if (devices[i].config->irq_count > 0 && devices[i].config->irqs) {
            for (int j = 0; j < devices[i].config->irq_count; j++) {
                dev_register_interrupt_device_model((int)devices[i].config->irqs[j], &elder_model_def);
            }
        }
	}
	return NULL;  // elder itself has no state to expose as opaque
}
