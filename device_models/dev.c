#include <qemu-plugin.h>
#include <utils.h>
#include <device.h>
#include <hw.h>

// Global hardware handle, if we use hardware.
static hw_t * hw;

static void dev_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
	unsigned int address = offset + 0x40000000;
	DEBUG_LOG("Attempting write: offset = 0x%" PRIx64 ", address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 "\n",
              offset, address, size, size * 2, value);

}

static uint64_t dev_read(void *opaque, hwaddr offset, unsigned size) {
	unsigned int address = offset + 0x40000000;
	uint64_t value =0;
	DEBUG_LOG("Attempting write: offset = 0x%" PRIx64 ", address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 "\n",
              offset, address, size, size * 2, value);

	return value;
	
}

// Base hardware reader and writer
DEV_XPORTER importer = {
        .read = dev_read,
        .write = dev_write
};

int dev_init(int argc, char ** argv) {
	char * dev_model_info = utils_get_arg("dev", argc, argv);

	if (dev_model_info) {
		qemu_plugin_unimp_export_device((void *)&importer);
	}

	return 0;
}
