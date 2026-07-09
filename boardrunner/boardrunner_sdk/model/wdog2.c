#include <stdint.h>
#include <device.h>

void* wdog2_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t wdog2_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void wdog2_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
