#include <stdint.h>
#include <device.h>

void* dbg_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t dbg_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void dbg_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
