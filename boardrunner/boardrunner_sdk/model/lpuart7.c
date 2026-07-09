#include <stdint.h>
#include <device.h>

void* lpuart7_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t lpuart7_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void lpuart7_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
