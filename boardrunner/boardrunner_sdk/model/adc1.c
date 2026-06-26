#include <stdint.h>
#include <device.h>

void* adc1_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t adc1_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void adc1_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
