#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdint.h>

#define MAX_DEVICES 64

typedef struct {
    char section[128];   // device name from INI section
    char libpath[256];
    uint64_t base;
    uint64_t size;
} device_config_t;

int device_parse_config(const char *filename, device_config_t *devices, int max_devices);

#endif

