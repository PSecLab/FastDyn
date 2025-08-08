#include <qemu-plugin.h>
#include <utils.h>
#include <device.h>
#include <hw.h>
#include "models.c"

// Global current device model 
DeviceModel * current = NULL;
FILE * io_logger;

static void dev_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
	unsigned int address = offset + 0x40000000;
	utils_log_to_file(io_logger,"Write: offset = 0x%" PRIx64 ", address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 "\n",
              offset, address, size, size * 2, value);

	current->write(opaque, address, value, size);

}

static uint64_t dev_read(void *opaque, hwaddr offset, unsigned size) {
	unsigned int address = offset + 0x40000000;
	uint64_t value;

	value = current->read(opaque, address, size);
	utils_log_to_file(io_logger, "Read: offset = 0x%" PRIx64 ", address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 "\n",
              offset, address, size, size * 2, value);

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

int dev_init(int argc, char ** argv) {
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
			qemu_plugin_unimp_export_device((void *)&importer);
			io_logger = fopen("io.log", "w");
			return current->init(arg);
		} else {
			utils_die("Incorrect device params");
		}
	}

	return 0;
}
