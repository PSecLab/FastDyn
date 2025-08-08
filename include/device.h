#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

typedef unsigned long hwaddr;

typedef struct unimp_exporter {
    uint64_t (*read)(void *opaque, hwaddr offset, unsigned size);
    void (*write)(void *opaque, hwaddr offset, uint64_t value, unsigned size);
} DEV_XPORTER;


int dev_init(int argc, char ** argv);

#endif // DEVICE_H
