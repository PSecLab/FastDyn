#include <stdint.h>
#include <device.h>

void* pwm3_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t pwm3_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void pwm3_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
