#include <stdint.h>
#include <device.h>

void* gpioc_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t gpioc_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void gpioc_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
