#include "device.h"
#include <stdio.h>

void systick_write(void *opaque, uint64_t addr, uint64_t value, unsigned size, uint64_t pc) {
    return;
}

uint64_t systick_read(void *opaque, uint64_t addr, unsigned size, uint64_t pc) {
    return 0;
}

void systick_init(DeviceModel *dev) {
    dev->write = systick_write;
    dev->read = systick_read;
    dev->opaque = NULL;
}
