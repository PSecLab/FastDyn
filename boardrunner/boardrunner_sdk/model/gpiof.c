#include <stdint.h>
#include <device.h>

void* gpiof_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t gpiof_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void gpiof_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
