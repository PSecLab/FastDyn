#include <stdint.h>
#include <device.h>

void* lpuart8_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t lpuart8_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void lpuart8_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
