#include <stdint.h>
#include <device.h>

void* dma0_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t dma0_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void dma0_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
