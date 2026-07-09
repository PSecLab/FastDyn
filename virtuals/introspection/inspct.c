#include <virtuals.h>
#include "inspct.h"
#include <core.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <utils.h>

//TODO: Fix this and make it modular
extern int inspct_freertos_init(int, char**);
extern int inspct_chibios_init(int, char**);
extern int inspct_zephyr_init(int, char**);
extern bool inspct_chibios_write_probe_json(FILE *f, const char *indent);
extern void inspct_chibios_write_summary_file(void);
extern bool inspct_zephyr_write_probe_json(FILE *f, const char *indent);
extern void inspct_zephyr_write_summary_file(void);

static int g_inspct_enabled = 0;
static char g_inspct_mode[16] = "off";
static char g_inspct_out_dir[512] = ".";
static uint32_t g_inspct_max_events = 4096;

static int inspct_mode_enabled(const char *mode) {
    if (!mode || mode[0] == '\0') {
        return 0;
    }
    if (!strcasecmp(mode, "off") || !strcasecmp(mode, "false") ||
        !strcmp(mode, "0") || !strcasecmp(mode, "none")) {
        return 0;
    }
    return 1;
}

int inspct_is_enabled(void) {
    return g_inspct_enabled;
}

const char *inspct_get_mode(void) {
    return g_inspct_mode;
}

const char *inspct_get_out_dir(void) {
    return g_inspct_out_dir;
}

uint32_t inspct_get_max_events(void) {
    return g_inspct_max_events;
}

bool inspct_write_probe_json(FILE *f, const char *indent) {
    if (!g_inspct_enabled || !f) {
        return false;
    }
    if (inspct_get_symbol("_kernel") != 0) {
        return inspct_zephyr_write_probe_json(f, indent ? indent : "");
    }
    return inspct_chibios_write_probe_json(f, indent ? indent : "");
}

void inspct_write_summary_file(void) {
    if (!g_inspct_enabled) {
        return;
    }
    if (inspct_get_symbol("_kernel") != 0) {
        inspct_zephyr_write_summary_file();
        return;
    }
    inspct_chibios_write_summary_file();
}

static void inspct_atexit(void) {
    inspct_write_summary_file();
}

int inspct_init(int argc, char ** argv, const char *schema_path) {
        const char *mode = utils_get_arg("rtos_introspection", argc, argv);
        if ((!mode || mode[0] == '\0') && schema_path && schema_path[0] != '\0') {
            mode = "summary";
        }
        if (!inspct_mode_enabled(mode)) {
            return 0;
        }
        if (!schema_path || schema_path[0] == '\0') {
            fprintf(stderr, "fastdyn: RTOS introspection requested without schema\n");
            return -1;
        }
        if (!load_fastdyn_schemas(schema_path)) {
            fprintf(stderr, "fastdyn: failed to load RTOS introspection schema: %s\n", schema_path);
            return -1;
        }

        snprintf(g_inspct_mode, sizeof(g_inspct_mode), "%s", mode);
        const char *out_dir = utils_get_arg("rtos_introspection_out", argc, argv);
        if (out_dir && out_dir[0] != '\0') {
            snprintf(g_inspct_out_dir, sizeof(g_inspct_out_dir), "%s", out_dir);
        }
        const char *max_events = utils_get_arg("rtos_introspection_max_events", argc, argv);
        if (max_events && max_events[0] != '\0') {
            unsigned long parsed = strtoul(max_events, NULL, 0);
            if (parsed > 0) {
                if (parsed > 65536) {
                    parsed = 65536;
                }
                g_inspct_max_events = (uint32_t)parsed;
            }
        }

        g_inspct_enabled = 1;
        core_register_exit_hook(inspct_atexit);

		//TODO: Initialize appropriately, we will need to have the OS as part of the arguments sent here.
		inspct_freertos_init(argc, argv);

		inspct_chibios_init(argc, argv);

        inspct_zephyr_init(argc, argv);

		return 0;
}
