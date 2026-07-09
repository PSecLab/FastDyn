#include <stdint.h>
#include <device.h>

void* wdog1_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t wdog1_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void wdog1_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
