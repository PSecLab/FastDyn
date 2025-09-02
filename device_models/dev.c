#include <qemu-plugin.h>
#include <time.h>
#include <utils.h>
#include <device.h>
#include <core.h>
#include "models.c"

// Global current device model
static FILE * io_logger;

static struct timespec start_ts;
static DeviceModel *device_lut[NUM_SLOTS];
static DeviceModel *system_lut[SYSTEM_NUM_SLOTS];

static inline DeviceModel **dev_select_lut(hwaddr addr) {
    switch (addr >> 28) { // top 4 bits
        case 0x4:
		case 0x5:
			return device_lut;  // Peripheral MMIO
        case 0xE:
			return system_lut;  // System Control Block
        default:
			return NULL;        // unknown region
    }
}

static inline unsigned dev_addr_to_slot(hwaddr addr, hwaddr REGION_BASE)
{
    return (addr - REGION_BASE) / SLOT_SIZE;
}





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

static int dev_write(char * handler, long unsigned int address, uint64_t value, long unsigned int size) {
	uint64_t pc = core_get_pc();
	time_t sec;
	long usec;
	dev_get_timestamp(&sec, &usec);
	DeviceModel **lut = dev_select_lut(address);
    hwaddr region_base = (lut == device_lut) ? DEVICE_BASE : SYSTEM_BASE;
	unsigned idx = dev_addr_to_slot(address, region_base);

	utils_log_to_file(io_logger,"[%5ld.%06ld] Write: \t address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 ", pc=0x%08X \n",
              sec, usec, address, size, size * 2, value, pc);

	DeviceModel *dev = (idx < NUM_SLOTS) ? lut[idx] : NULL;
	if (dev) {
			dev->write(dev->opaque, address, value, size);
	}

	if (handler && (handler[0] == 'g')) {
           return 0;
    }

	// Continue internal operation
	return 1;
}

static int dev_read(char * handler, long unsigned int address, uint64_t *buf, long unsigned int size) {
	uint64_t pc = core_get_pc();
	uint64_t value;
	time_t sec;
    long usec;
	DeviceModel **lut = dev_select_lut(address);
	hwaddr region_base = (lut == device_lut) ? DEVICE_BASE : SYSTEM_BASE;
	unsigned idx = dev_addr_to_slot(address, region_base);
    dev_get_timestamp(&sec, &usec);


	DeviceModel *dev = (idx < NUM_SLOTS) ? lut[idx] : NULL;
    if (dev) {
        value = dev->read(dev->opaque, address, size);
		utils_log_to_file(io_logger, "[%5ld.%06ld] Read: \t address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 ", pc=0x%08X \n",
              sec, usec, address, size, size * 2, value, pc);

		*buf = value;
	}
	else {
		dev_debug("IO Access not handled");
	}

	if (handler && (handler[0] == 'g')) {
		return 0;
	}

	// Continue internal operation
	return 1;

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
#if 0
	DeviceModel *dev = (idx < NUM_SLOTS) ? device_lut[idx] : NULL;
    if (dev) {
        dev->current(number);
    }
#endif

}

void dev_irqret_hook(int number) {
    time_t sec;
    long usec;
    dev_get_timestamp(&sec, &usec);
    utils_log_to_file(io_logger, "[%5ld.%06ld] Interrupt Served: \t Vector = 0x%08X\n",
              sec, usec, number);
#if 0
	DeviceModel *dev = (idx < NUM_SLOTS) ? device_lut[idx] : NULL;
	if (dev) {
		dev->serve(number);
	}
#endif

}

void dev_register_device_model(hwaddr start, hwaddr end, DeviceModel *dev) {
	DeviceModel **lut = dev_select_lut(start);
	hwaddr region_base = (lut == device_lut) ? DEVICE_BASE : SYSTEM_BASE;
    unsigned idx_start = dev_addr_to_slot(start, region_base);
    unsigned idx_end   = dev_addr_to_slot(end, region_base);

    if (idx_end >= NUM_SLOTS) idx_end = NUM_SLOTS - 1;

    for (unsigned i = idx_start; i <= idx_end; i++) {
        lut[i] = dev;
    }
}

static inline int parse_models(const char *s, ModelEntry *entries, int max_entries) {
    // Skip "dev=" prefix if present
    if (strncmp(s, "dev=", 4) == 0) {
        s += 4;
    }

    int count = 0;
    char buffer[256];
    strncpy(buffer, s, sizeof(buffer));
    buffer[sizeof(buffer)-1] = '\0';

    char *token = strtok(buffer, "%");
    while (token && count < max_entries) {
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            strncpy(entries[count].model, token, sizeof(entries[count].model));
            entries[count].model[sizeof(entries[count].model)-1] = '\0';

            strncpy(entries[count].args, colon + 1, sizeof(entries[count].args));
            entries[count].args[sizeof(entries[count].args)-1] = '\0';
        } else {
            // No colon found, treat entire string as model, empty args
            strncpy(entries[count].model, token, sizeof(entries[count].model));
            entries[count].model[sizeof(entries[count].model)-1] = '\0';
            entries[count].args[0] = '\0';
        }
        count++;
        token = strtok(NULL, "%");
    }

    return count;
}


int dev_init(int argc, char ** argv) {
	char * dev_model_info = utils_get_arg("dev", argc, argv);

	if (dev_model_info) {
		// Regisgter IRQ listener for logging
	    qemu_plugin_register_irq_hook(dev_notify_irq, dev_irqret_hook);
		qemu_plugin_unimp_export_device((void *)&importer);
		io_logger = fopen("io.log", "w");

		if (start_ts.tv_sec == 0 && start_ts.tv_nsec == 0) {
			    clock_gettime(CLOCK_MONOTONIC, &start_ts);
		}
		ModelEntry entries[10];
		int n = parse_models(dev_model_info, entries, 10);

		for (int i = 0; i < n; i++) {
				DeviceModel * current = find_device_model(entries[i].model);
				if (!current) {
						utils_die("Device Model not found.");
				}

				current->init(entries[i].args);
    	}
	}

	return 0;
}
