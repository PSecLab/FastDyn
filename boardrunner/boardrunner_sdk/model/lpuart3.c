#include <stdint.h>
#include <device.h>

void* lpuart3_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t lpuart3_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void lpuart3_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
