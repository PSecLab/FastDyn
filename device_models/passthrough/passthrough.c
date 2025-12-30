#include <device.h>
#include <hw.h>
#include <utils.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

// Global hardware handle and mutex
static hw_t *hw = NULL;
static pthread_t dev_thread;
static pthread_mutex_t hw_mutex = PTHREAD_MUTEX_INITIALIZER;
hwaddr buffer_base   = 0x20000000;
hwaddr buffer_limit  = 0x20000200;
hwaddr buffer_address;
bool   start_buffering = false;
int total_comes =0;


static uint64_t passthrough_read(void *opaque, hwaddr address, unsigned size, uint64_t pc) {
    (void)opaque;
    uint32_t value_read;

    pthread_mutex_lock(&hw_mutex);
    if (!hw) {
        pthread_mutex_unlock(&hw_mutex);
        utils_die("HW handle not initialized");
    }

    // Nov 9 logic
    //In case of a read to STA register when start_buffering is set, just simply send 0x00045200 to continue the while loop
    // if ((address == 0x40012C34) && (start_buffering) && ((pc == 0x08003170) | (pc == 0x08003176))){
    //     // return 0x00045000;
    //     total_comes+=1;
    //     printf("I am still being used %d\n", total_comes);
    //     pthread_mutex_unlock(&hw_mutex);
    //     return 282624;
    // }

    int status = hw_read32(hw, address, &value_read);

    pthread_mutex_unlock(&hw_mutex);

    if (status != 0) {
        utils_die("HW Read Failed");
    }

    return value_read;
}

static void passthrough_write(void *opaque, hwaddr address, uint64_t value, unsigned size, uint64_t pc) {
    (void)opaque;

    pthread_mutex_lock(&hw_mutex);
    if (!hw) {
        pthread_mutex_unlock(&hw_mutex);
        utils_die("HW handle not initialized");
    }

    // Nov 9 Logic
    // start buffering as soon as we see a write to the control register
    // if ((address == 0x40012C2C) && (value == 0x91)) {
    //     start_buffering = true;
    //     buffer_address   = buffer_base;   // reset
    // }

    // //buffer the data till we reach max size of 512B
    // if ((address == 0x40012C80) && start_buffering) {             //this address is FIFO Register address
    //     address = buffer_address;
    //     buffer_address = buffer_address + 4;
    // }

    // if ((buffer_address == buffer_limit) && start_buffering) {
    //     total_comes+=1;
    //     // if (total_comes==2){utils_die("die");}
    //     start_buffering = false;
    //     hw_board_halt(hw);          //halt the board
    //     // printf("The current value of pc %lx\n", hw_read_reg(hw,15));
    //     uint64_t lr_val = hw_read_reg(hw,14);   //read lr
    //     hw_write_reg(hw,15, lr_val | 1);//update pc
    //     hw_board_step(hw);  //move to next instruction in server firmware
    //     // printf("The current value of lr %lx\n", hw_read_reg(hw,14));
    //     // printf("The current value of pc %lx\n", hw_read_reg(hw,15));
    //     // // while(1);
    //     hw_board_run(hw);   //run the board
    //     uint32_t value_read2;
    //     int read_status = hw_read32(hw, 0x20000800, &value_read2);
    //     while((value_read2 != 24223)){    //wait for the stop condition
    //         if (value_read2 == 105) {
    //             while(1){//stuck in the loop in case of failure signal from the server firmware
    //                 printf("Current signal value from the server firmware:: %x\n", value_read2);
    //             }
    //         }
    //         printf("Current signal value from the server firmware:: %x\n", value_read2);
    //         read_status = hw_read32(hw, 0x20000800, &value_read2);
    //     }
    // }



    int status;
    if (size ==1) {
        status = hw_write8(hw, address, (uint32_t)value);
    } else {
        status = hw_write32(hw, address, (uint32_t)value);
    }
    pthread_mutex_unlock(&hw_mutex);

    if (status != 0) {
        utils_die("HW Write Failed");
    }
}
static pthread_cond_t irq_cv = PTHREAD_COND_INITIALIZER;
static int irq_pending;

static void* dev_thread_fn(void* arg) {
    (void)arg;
    // while(1);
    while (1) {
#ifdef TEST_INTERRUPT_THREAD
        sleep(5);
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
				qemu_plugin_raise_irq(firing_line, false);
		} else {
			pthread_mutex_unlock(&hw_mutex);
			// usleep(40000);
            sleep(5);
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

static int passthrough_init(ConfigSection* model_info);
// The public definition of the passthrough device model
DeviceModel passthrough_model_def = {
    .name = "passthrough",
    .read = passthrough_read,
    .write = passthrough_write,
    .init = passthrough_init,
    .serve = passthrough_serve,
    .interrupt = passthrough_interrupt,
};

static int passthrough_init(ConfigSection* model_info) {
    //Find the overall ranges for all the devices registered as passthrough
    Range ranges[10];
    utils_parse_ranges(model_info->overall_range_count,model_info->overall_ranges, ranges);

    //need a backend for the passthrough
    hw = hw_connect(model_info->backend, NULL, 0);
	if (!hw) {
        utils_die("HW connection failed.");
        return 1;
    }

    if (pthread_create(&dev_thread, NULL, dev_thread_fn, NULL) != 0) {
        perror("Failed to create thread");
        return 1;
    }

	for (int i = 0; i < model_info->overall_range_count; i++) {
        dev_register_device_model(ranges[i].start, ranges[i].end, &passthrough_model_def);
    }

    //Issue?:Right now, we register the IRQ for the complete device model instead of the specific device
    //Register the device models for the interrupts registered by the user.
    //Find if any of the device wants to register an IRQ
    //Just register for the first occuring irq numbers by any device
    for (int i=0; i< model_info->device_count; i++) {
        DeviceModels* d = &model_info->devices[i];
        if (d->irq[0]) {
        int int_nums;   //number of interrupts registered by the user

        //0-10:20-25 -> 0 to 10 and 20 to 25 interrupts supported
        int *int_lst = utils_parse_interrupt_ranges(d->irq, &int_nums);

        for (int i =0; i < int_nums; i++) {
            dev_register_interrupt_device_model(int_lst[i], &passthrough_model_def);
            }
        }
        break;
    }
    return 0;
}



