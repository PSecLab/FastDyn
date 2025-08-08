#include <device.h>
#include <hw.h>
#include <utils.h>

// Global hardware handle, if we use hardware.
static hw_t * hw;

static uint64_t passthrough_read(void *opaque, hwaddr address, unsigned size) {
	uint64_t return_val;
	uint32_t value_read;
	int status = hw_read32(hw, address, &value_read);
	if (status != 0) {
			utils_die("HW Read Failed");
	}
	return_val = value_read;
	return return_val;
}

static void passthrough_write(void *opaque, hwaddr address, uint64_t value, unsigned size) {
	int status = hw_write32(hw, address, (uint32_t)value); 

	if (status != 0) {
			utils_die("HW Write Failed");
	}
}

static int passthrough_init(char * argument) {
	hw = hw_connect(argument, NULL, 0);

	if (!hw) {
			utils_die("HW connection failed.");
	}

	return 0;
}

// The public definition of the passthrough device model
DeviceModel passthrough_model_def = {
    .name = "passthrough",
    .read = passthrough_read,
    .write = passthrough_write,
	.init = passthrough_init,
};

