#include <stdint.h>
#include <device.h>

void* iwdg_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t iwdg_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void iwdg_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
