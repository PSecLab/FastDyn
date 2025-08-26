#include <device.h>
#include <hw.h>
#include <utils.h>
#include <pthread.h>
#include <unistd.h>

// Global hardware handle and mutex
static hw_t *hw = NULL;
static pthread_t dev_thread;
static pthread_mutex_t hw_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint64_t passthrough_read(void *opaque, hwaddr address, unsigned size) {
    (void)opaque;
    uint32_t value_read;

    pthread_mutex_lock(&hw_mutex);
    if (!hw) {
        pthread_mutex_unlock(&hw_mutex);
        utils_die("HW handle not initialized");
    }

    int status = hw_read32(hw, address, &value_read);
    pthread_mutex_unlock(&hw_mutex);

    if (status != 0) {
        utils_die("HW Read Failed");
    }

    return value_read;
}

static void passthrough_write(void *opaque, hwaddr address, uint64_t value, unsigned size) {
    (void)opaque;

    pthread_mutex_lock(&hw_mutex);
    if (!hw) {
        pthread_mutex_unlock(&hw_mutex);
        utils_die("HW handle not initialized");
    }

    int status = hw_write32(hw, address, (uint32_t)value);
    pthread_mutex_unlock(&hw_mutex);

    if (status != 0) {
        utils_die("HW Write Failed");
    }
}
static pthread_cond_t irq_cv = PTHREAD_COND_INITIALIZER;
static int irq_pending;
static void* dev_thread_fn(void* arg) {
    (void)arg;

    while (1) {
#ifdef TEST_INTERRUPT_THREAD
        sleep(10);
        qemu_plugin_raise_irq(15);
#endif
		pthread_mutex_lock(&hw_mutex);
		// Wait until no IRQ is pending
        while (irq_pending) {
            pthread_cond_wait(&irq_cv, &hw_mutex);
        }


		if (hw_board_halted(hw)) {
				for (int i =0; i<16; i++) {
					dev_debug("Register%d: 0x%lx\n", i, hw_read_reg(hw, i));
				}
				int firing_line = hw_read_reg(hw, 0);
				irq_pending = firing_line;

				dev_debug("Register%d: 0x%lx\n", 0, hw_read_reg(hw, 0));
                dev_debug("Register%d: 0x%lx\n", 15, hw_read_reg(hw, 15));
				pthread_mutex_unlock(&hw_mutex);
				qemu_plugin_raise_irq(firing_line);
		} else {
			pthread_mutex_unlock(&hw_mutex);
			usleep(40000);
		}
    }

    return NULL;
}

static int passthrough_serve(int line) {
	pthread_mutex_lock(&hw_mutex);
	hw_write_reg(hw, 1, line);
	hw_board_run(hw);
	irq_pending =0;
	pthread_cond_signal(&irq_cv);
	pthread_mutex_unlock(&hw_mutex);
	return 0;
}

static int passthrough_interrupt(int line) {
		(void)line;
		return 0;
}

static int passthrough_init(char *argument) {
    hw = hw_connect(argument, NULL, 0);
    if (!hw) {
        utils_die("HW connection failed.");
        return 1;
    }

    if (pthread_create(&dev_thread, NULL, dev_thread_fn, NULL) != 0) {
        perror("Failed to create thread");
        return 1;
    }

    return 0;
}


// The public definition of the passthrough device model
DeviceModel passthrough_model_def = {
    .name = "passthrough",
    .read = passthrough_read,
    .write = passthrough_write,
	.init = passthrough_init,
	.serve = passthrough_serve,
	.interrupt = passthrough_interrupt,
};

