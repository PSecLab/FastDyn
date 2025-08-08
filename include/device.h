#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

typedef unsigned long hwaddr;

/**
 * @brief Function pointer for a device write operation.
 */
typedef void (*DeviceWriteFunc)(void *opaque, hwaddr offset, uint64_t value, unsigned size);

/**
 * @brief Function pointer for a device read operation.
 */
typedef uint64_t (*DeviceReadFunc)(void *opaque, hwaddr offset, unsigned size);

typedef int (*DeviceInit)(char * args);

/**
 * @brief Represents a device model, containing its name and I/O functions.
 */
typedef struct DeviceModel {
    const char *name;
    DeviceReadFunc read;
    DeviceWriteFunc write;
	DeviceInit init;
} DeviceModel;

typedef struct unimp_exporter {
    uint64_t (*read)(void *opaque, hwaddr offset, unsigned size);
    void (*write)(void *opaque, hwaddr offset, uint64_t value, unsigned size);
} DEV_XPORTER;


int dev_init(int argc, char ** argv);
#endif // DEVICE_H
