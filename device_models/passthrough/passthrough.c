#include <device.h>
#include <hw.h>
#include <utils.h>
#include <pthread.h>
#include <unistd.h>

// Global hardware handle, if we use hardware.
static hw_t * hw;
static pthread_t dev_thread;

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

#define TEST_INTERRUPT_THREAD
static void* dev_thread_fn(void* arg) {
    (void)arg;
	
	/* Thread will continously monitor */ 
	while (1<2) {
#ifdef TEST_INTERRUPT_THREAD
			sleep(10);
			qemu_plugin_raise_irq(15);
#endif

	}
    return NULL;
}

static int passthrough_init(char * argument) {
	hw = hw_connect(argument, NULL, 0);

	// Create the thread
    if (pthread_create(&dev_thread, NULL, dev_thread_fn, NULL) != 0) {
        perror("Failed to create thread");
        return 1;
    }

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

