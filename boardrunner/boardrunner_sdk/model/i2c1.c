#include <stdint.h>
#include <device.h>

void* i2c1_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t i2c1_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void i2c1_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
