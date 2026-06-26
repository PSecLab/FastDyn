#include <stdint.h>
#include <device.h>

void* gpiog_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t gpiog_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void gpiog_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
