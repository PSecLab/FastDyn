#include <stdint.h>
#include <device.h>

void* otg_fs_device_init(ConfigSection* model_info) {
    return NULL;
}

uint64_t otg_fs_device_read(void *opaque, uint64_t addr, unsigned size) {
    return 0;
}

void otg_fs_device_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    return;
}
