#include <device.h>
#include <utils.h>
#include <pthread.h>
#include <unistd.h>

static uint64_t classic_read(void *opaque, hwaddr address, unsigned size) {
    (void)opaque;
    return 0;
}

static void classic_write(void *opaque, hwaddr address, uint64_t value, unsigned size) {
    (void)opaque;
}
static int classic_serve(int line) {
	printf("Served Line: %d \n", line);
	return 0;
}

static int classic_interrupt(int line) {
		(void)line;
		printf("Interrupt Line: %d \n", line);
		return 0;
}

static int classic_init(char *argument) {
    return 0;
}


// The public definition of the classic device model
DeviceModel classic_model_def = {
    .name = "classic",
    .read = classic_read,
    .write = classic_write,
	.init = classic_init,
	.serve = classic_serve,
	.interrupt = classic_interrupt,
};

