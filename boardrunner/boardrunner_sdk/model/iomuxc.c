#include <stdint.h>
#include <device.h>

void* iomuxc_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t iomuxc_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void iomuxc_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
