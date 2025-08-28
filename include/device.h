#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include <qemu/qemu-plugin.h>

#ifdef ENABLE_DEV_DEBUG
    #define dev_debug(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
    #define dev_debug(fmt, ...) do {} while (0)
#endif

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

typedef int (*DeviceIRQFunc)(int);

/**
 * @brief Represents a device model, containing its name and I/O functions.
 */
typedef struct DeviceModel {
    const char *name;
    DeviceReadFunc read;
    DeviceWriteFunc write;
	DeviceInit init;
	DeviceIRQFunc serve;
	DeviceIRQFunc interrupt;
} DeviceModel;

typedef struct {
    char model[64];
    char args[64];
} ModelEntry;


typedef struct unimp_exporter {
    int (*read)(char * handler, hwaddr addr, uint64_t *buf, hwaddr len);
    int (*write)(char * handler, hwaddr addr, uint64_t value, hwaddr len);
} DEV_XPORTER;

//4KB regions for now.
#define SLOT_SIZE   0x1000

#define DEVICE_BASE 0x40000000ULL
#define DEVICE_SIZE 0x20000000ULL  // 0x40000000 -> 0x5FFFFFFF, ~512 MB
#define NUM_SLOTS   (DEVICE_SIZE / SLOT_SIZE)

// System Control Block → system_lut
#define SYSTEM_BASE 0xE000E000ULL
#define SYSTEM_SIZE 0x1000
#define SYSTEM_NUM_SLOTS (SYSTEM_SIZE / SLOT_SIZE)


int dev_init(int argc, char ** argv);
void dev_register_device_model(hwaddr start, hwaddr end, DeviceModel *dev);
#endif // DEVICE_H
