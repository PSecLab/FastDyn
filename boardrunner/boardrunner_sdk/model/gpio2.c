#include <stdint.h>
#include <device.h>

void* gpio2_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t gpio2_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void gpio2_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
