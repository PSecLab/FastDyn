#include <stdint.h>
#include <device.h>

void* lpuart4_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t lpuart4_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void lpuart4_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
