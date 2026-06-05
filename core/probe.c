#include "probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "cJSON.h"
#include "utils.h"

// Polling loop detection thresholds
#define PROBE_POLL_THRESHOLD 1000

// Fault tracking
#define MAX_FAULTS 10
static uint64_t fault_addrs[MAX_FAULTS];
static char *fault_names[MAX_FAULTS];
static int num_faults = 0;

// State tracking for polling loops
static uint64_t last_read_pc = 0;
static uint64_t last_read_addr = 0;
static int read_streak = 0;

static char probe_out_dir[512] = ".";

// Write result and exit QEMU
static void probe_exit_with_result(const char *reason, uint64_t pc, uint64_t extra_info) {
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/probe_result.json", probe_out_dir);

    FILE *f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "{\n");
        fprintf(f, "  \"exit_reason\": \"%s\",\n", reason);
        fprintf(f, "  \"pc\": \"0x%08" PRIx64 "\",\n", pc);
        fprintf(f, "  \"extra_info\": \"0x%08" PRIx64 "\"\n", extra_info);
        fprintf(f, "}\n");
        fclose(f);
    } else {
        fprintf(stderr, "[Probe] Failed to write %s\n", filepath);
    }
    
    printf("[Probe] Exiting due to %s at PC 0x%08" PRIx64 "\n", reason, pc);
    // Request plugin to exit QEMU abruptly
    exit(0);
}

// TB translation callback to detect fault handlers
static void probe_vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    
    for (int i = 0; i < num_faults; i++) {
        if (pc == fault_addrs[i]) {
            // Hit a fault handler!
            printf("[Probe] Hit fault handler: %s\n", fault_names[i]);
            probe_exit_with_result(fault_names[i], pc, 0);
        }
    }
}

// Helper to read entire file
static char* read_entire_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = (char*)malloc(length + 1);
    if (data) {
        fread(data, 1, length, f);
        data[length] = '\0';
    }
    fclose(f);
    return data;
}

void probe_init(const char *faults_json_path, const char *out_dir) {
    if (out_dir) {
        strncpy(probe_out_dir, out_dir, sizeof(probe_out_dir) - 1);
    }
    
    if (!faults_json_path) return;
    
    char *json_data = read_entire_file(faults_json_path);
    if (!json_data) {
        fprintf(stderr, "[Probe] Could not read faults JSON: %s\n", faults_json_path);
        return;
    }
    
    cJSON *root = cJSON_Parse(json_data);
    if (root && cJSON_IsArray(root)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, root) {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
            cJSON *handler = cJSON_GetObjectItemCaseSensitive(entry, "handler");
            
            if (cJSON_IsString(name) && cJSON_IsString(handler) && num_faults < MAX_FAULTS) {
                fault_names[num_faults] = strdup(name->valuestring);
                fault_addrs[num_faults] = strtoull(handler->valuestring, NULL, 0);
                // Strip thumb bit if present (bit 0)
                fault_addrs[num_faults] &= ~1ULL;
                num_faults++;
            }
        }
    }
    
    cJSON_Delete(root);
    free(json_data);
    
    printf("[Probe] Initialized. Monitoring %d fault handlers.\n", num_faults);
}

void probe_check_read(uint64_t pc, uint64_t addr) {
    if (pc == last_read_pc && addr == last_read_addr) {
        read_streak++;
        if (read_streak >= PROBE_POLL_THRESHOLD) {
            probe_exit_with_result("polling_loop", pc, addr);
        }
    } else {
        last_read_pc = pc;
        last_read_addr = addr;
        read_streak = 1;
    }
}

void probe_check_unhandled_access(uint64_t pc, uint64_t addr, int is_write) {
    if (is_write) {
        probe_exit_with_result("unhandled_mmio_write", pc, addr);
    } else {
        probe_exit_with_result("unhandled_mmio_read", pc, addr);
    }
}

// Expose TB trans hook registration for core.c
void probe_register_hooks(qemu_plugin_id_t id) {
    qemu_plugin_register_vcpu_tb_trans_cb(id, probe_vcpu_tb_trans);
}
