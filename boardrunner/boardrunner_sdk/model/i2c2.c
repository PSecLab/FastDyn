#include <stdint.h>
#include <device.h>

void* i2c2_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t i2c2_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void i2c2_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
