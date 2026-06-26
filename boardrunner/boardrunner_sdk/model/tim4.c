#include <stdint.h>
#include <device.h>

void* tim4_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t tim4_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void tim4_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
