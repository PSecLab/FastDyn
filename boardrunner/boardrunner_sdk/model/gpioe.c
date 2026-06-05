#include <stdint.h>
#include <device.h>

void* gpioe_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t gpioe_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void gpioe_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
