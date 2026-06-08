#include <stdint.h>
#include <device.h>

void* coredebug_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t coredebug_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void coredebug_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
