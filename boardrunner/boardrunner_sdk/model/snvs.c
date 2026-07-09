#include <stdint.h>
#include <device.h>

void* snvs_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t snvs_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void snvs_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
