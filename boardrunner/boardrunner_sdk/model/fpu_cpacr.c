#include <stdint.h>
#include <device.h>

void* fpu_cpacr_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t fpu_cpacr_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void fpu_cpacr_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
