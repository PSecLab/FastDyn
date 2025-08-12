#include <qemu-plugin.h>
#include <time.h>
#include <utils.h>
#include <device.h>
#include <hw.h>
#include <core.h>
#include "models.c"

// Global current device model 
static DeviceModel * current = NULL;
static FILE * io_logger;

static struct timespec start_ts;



static inline void dev_get_timestamp(time_t *sec, long *usec) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	// Calculate offset from start
	*sec = ts.tv_sec - start_ts.tv_sec;
	*usec = (ts.tv_nsec - start_ts.tv_nsec) / 1000;
	if (*usec < 0) {
    	*sec -= 1;
	    *usec += 1000000;
	}
}

static void dev_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
	unsigned int address = offset + 0x40000000;
	uint64_t pc = core_get_pc();
	time_t sec;
	long usec;
	dev_get_timestamp(&sec, &usec);

	utils_log_to_file(io_logger,"[%5ld.%06ld] Write: \t address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 ", pc=0x%08X \n",
              sec, usec, address, size, size * 2, value, pc);

	current->write(opaque, address, value, size);

}

static uint64_t dev_read(void *opaque, hwaddr offset, unsigned size) {
	unsigned int address = offset + 0x40000000;
	uint64_t pc = core_get_pc();
	uint64_t value;
	time_t sec;
    long usec;
    dev_get_timestamp(&sec, &usec);



	value = current->read(opaque, address, size);
	utils_log_to_file(io_logger, "[%5ld.%06ld] Read: \t address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 ", pc=0x%08X \n",
              sec, usec, address, size, size * 2, value, pc);

	return value;
	
}

// Base hardware reader and writer
static DEV_XPORTER importer = {
        .read = dev_read,
        .write = dev_write
};

DeviceModel* find_device_model(const char *name) {
    for (int i = 0; i < num_devices; i++) {
        if (strcmp(all_devices[i]->name, name) == 0) {
            return all_devices[i];
        }
    }
    return NULL;
}

void dev_notify_irq(int number) {
	time_t sec;
    long usec;
    dev_get_timestamp(&sec, &usec);
	utils_log_to_file(io_logger, "[%5ld.%06ld] Interrupt Taken: \t Vector = 0x%08X\n",
              sec, usec, number);

}

int dev_init(int argc, char ** argv) {
	// Regisgter IRQ listener for logging
	qemu_plugin_register_irq_hook(dev_notify_irq);
	char * dev_model_info = utils_get_arg("dev", argc, argv);

	if (dev_model_info) {
		char *sep = strchr(dev_model_info, ':');
		if (sep) {
			*sep = '\0';
			char *name = dev_model_info;
	        char *arg = sep + 1;

			current = find_device_model(name); 

			if (!current) {
					utils_die("Device Model not found.");
			}

			// Generic Things 
			qemu_plugin_unimp_export_device((void *)&importer);
			io_logger = fopen("io.log", "w");
			if (start_ts.tv_sec == 0 && start_ts.tv_nsec == 0) {
			    clock_gettime(CLOCK_MONOTONIC, &start_ts);
			}

			// Model init
			return current->init(arg);
		} else {
			utils_die("Incorrect device params");
		}
	}

	return 0;
}
