#include "device_config.h"
#include "ini.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    device_config_t *devices;
    int max;
    int count;
    char current_section[128];
} parse_ctx_t;

static int handler(void *user, const char *section, const char *name, const char *value) {
    parse_ctx_t *ctx = (parse_ctx_t *)user;

    // New section? Add a device entry
    if (strcmp(ctx->current_section, section) != 0) {
        if (ctx->count >= ctx->max) return 0; // too many devices
        strncpy(ctx->devices[ctx->count].section, section, sizeof(ctx->devices[ctx->count].section)-1);
        ctx->current_section[0] = '\0';
        strncpy(ctx->current_section, section, sizeof(ctx->current_section)-1);
    }

    device_config_t *dev = &ctx->devices[ctx->count];

    if (strcmp(name, "libpath") == 0) {
        strncpy(dev->libpath, value, sizeof(dev->libpath)-1);
    } else if (strcmp(name, "base") == 0) {
        dev->base = strtoull(value, NULL, 0); // base 0 → hex or decimal
    } else if (strcmp(name, "size") == 0) {
        dev->size = strtoull(value, NULL, 0);
    }

    return 1;
}

int parse_config(const char *filename, device_config_t *devices, int max_devices) {
    parse_ctx_t ctx = {
        .devices = devices,
        .max = max_devices,
        .count = 0,
        .current_section = ""
    };

    // ini_parse calls handler() for each key=value pair
    if (ini_parse(filename, handler, &ctx) < 0) {
        return -1;
    }

    // Count devices: any section name seen counts as a device
    for (int i = 0; i < max_devices; i++) {
        if (devices[i].section[0] != '\0') {
            ctx.count++;
        }
    }
    return ctx.count;
}

