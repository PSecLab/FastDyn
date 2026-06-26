#include <stdint.h>
#include <device.h>

void* spi1_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t spi1_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void spi1_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
