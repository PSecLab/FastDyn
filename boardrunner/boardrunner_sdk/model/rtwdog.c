#include <stdint.h>
#include <device.h>

void* rtwdog_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t rtwdog_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void rtwdog_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
