#include "probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "cJSON.h"
#include "utils.h"
#include <core.h>

// Polling loop detection thresholds
#define PROBE_POLL_THRESHOLD 1000

// Fault tracking
#define MAX_FAULTS 20
static uint64_t fault_addrs[MAX_FAULTS];
static char *fault_names[MAX_FAULTS];
static int num_faults = 0;

static uint64_t default_handler_addr = 0;
static uint64_t reset_handler_addr = 0;
static int reset_handler_hits = 0;

#define MAX_PANICS 200
static uint64_t panic_addrs[MAX_PANICS];
static int num_panics = 0;

#define MAX_BOUNDS 20
static uint64_t bounds_start[MAX_BOUNDS];
static uint64_t bounds_end[MAX_BOUNDS];
static int num_bounds = 0;


// Milestones
#define MAX_MILESTONES 50
static uint64_t milestone_addrs[MAX_MILESTONES];
static int num_milestones = 0;

// BBL Trace
#define BBL_TRACE_SIZE 10000
static uint64_t bbl_trace[BBL_TRACE_SIZE];
static int bbl_idx = 0;

// Coverage tracking
static uint64_t instruction_count = 0;
static uint64_t last_novel_instruction_count = 0;
#define NO_NEW_COVERAGE_THRESHOLD 1000000

// Generalized polling
static uint64_t last_mmio_read_addr = 0;
static uint64_t last_mmio_read_value = 0;
static int reads_since_last_mmio_write = 0;

static char probe_out_dir[512] = ".";

static bool probe_result_written = false;

// Write result to file
static void probe_write_result(const char *reason, uint64_t pc, uint64_t extra_info) {
    if (probe_result_written) return;
    probe_result_written = true;

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/probe_result.json", probe_out_dir);

    FILE *f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "{\n");
        fprintf(f, "  \"exit_reason\": \"%s\",\n", reason);
        fprintf(f, "  \"pc\": \"0x%08" PRIx64 "\",\n", pc);
        fprintf(f, "  \"extra_info\": \"0x%08" PRIx64 "\",\n", extra_info);

        fprintf(f, "  \"bbl_trace\": [\n");
        int count = (bbl_idx < BBL_TRACE_SIZE) ? bbl_idx : BBL_TRACE_SIZE;
        int start = (bbl_idx < BBL_TRACE_SIZE) ? 0 : (bbl_idx % BBL_TRACE_SIZE);
        for (int i = 0; i < count; i++) {
            fprintf(f, "    \"0x%08" PRIx64 "\"%s\n", bbl_trace[(start + i) % BBL_TRACE_SIZE], (i == count - 1) ? "" : ",");
        }
        fprintf(f, "  ]\n");

        fprintf(f, "}\n");
        fclose(f);
    } else {
        fprintf(stderr, "[Probe] Failed to write %s\n", filepath);
    }
    
    printf("[Probe] Exiting due to %s at PC 0x%08" PRIx64 "\n", reason, pc);
}

// Write result and exit QEMU
static void probe_exit_with_result(const char *reason, uint64_t pc, uint64_t extra_info) {
    probe_write_result(reason, pc, extra_info);
    // Request plugin to exit QEMU abruptly
    exit(0);
}

static void probe_atexit(void) {
    probe_write_result("manual_stop", 0, 0);
}

// TB translation callback to detect fault handlers
static void probe_vcpu_tb_exec(unsigned int vcpu_index, void *userdata) {
    uint64_t pc = (uint64_t)userdata;
    bbl_trace[bbl_idx++ % BBL_TRACE_SIZE] = pc;
    instruction_count++;
    
    if (instruction_count - last_novel_instruction_count > NO_NEW_COVERAGE_THRESHOLD) {
        probe_exit_with_result("no_new_coverage", pc, 0);
    }
}

static void probe_wfi_exec(unsigned int vcpu_index, void *userdata) {
    uint64_t pc = (uint64_t)userdata;
    probe_exit_with_result("wfi_idle_starvation", pc, 0);
}

static void probe_vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    
    // Register execution hook
    qemu_plugin_register_vcpu_tb_exec_cb(tb, probe_vcpu_tb_exec, QEMU_PLUGIN_CB_NO_REGS, (void *)pc);
    
    // Update novel block count
    last_novel_instruction_count = instruction_count;

    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        if (qemu_plugin_insn_size(insn) == 2) {
            uint8_t data[2];
            if (qemu_plugin_insn_data(insn, data, 2) == 2) {
                if (data[0] == 0x30 && data[1] == 0xbf) { // WFI opcode
                    qemu_plugin_register_vcpu_insn_exec_cb(insn, probe_wfi_exec, QEMU_PLUGIN_CB_NO_REGS, (void *)qemu_plugin_insn_vaddr(insn));
                }
            }
        }
    }
    
    // Milestones
    for (int i = 0; i < num_milestones; i++) {
        if (pc == milestone_addrs[i]) {
            probe_exit_with_result("milestone_reached", pc, 0);
        }
    }

    // Bounds checking
    if (num_bounds > 0) {
        int valid = 0;
        for (int i = 0; i < num_bounds; i++) {
            if (pc >= bounds_start[i] && pc < bounds_end[i]) {
                valid = 1;
                break;
            }
        }
        if (!valid) {
            probe_exit_with_result("invalid_pc", pc, 0);
        }
    }

    if (default_handler_addr != 0 && pc == default_handler_addr) {
        probe_exit_with_result("unexpected_default_irq", pc, 0);
    }

    if (reset_handler_addr != 0 && pc == reset_handler_addr) {
        reset_handler_hits++;
        if (reset_handler_hits > 1) {
            probe_exit_with_result("reset_loop", pc, 0);
        }
    }

    for (int i = 0; i < num_panics; i++) {
        if (pc == panic_addrs[i]) {
            probe_exit_with_result("panic_or_assert", pc, 0);
        }
    }

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

void probe_init(const char *faults_json_path, const char *milestones_json_path, const char *out_dir) {
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
    if (root && cJSON_IsObject(root)) {
        cJSON *faults = cJSON_GetObjectItemCaseSensitive(root, "faults");
        if (faults && cJSON_IsArray(faults)) {
            cJSON *entry;
            cJSON_ArrayForEach(entry, faults) {
                cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
                cJSON *handler = cJSON_GetObjectItemCaseSensitive(entry, "handler");
                if (cJSON_IsString(name) && cJSON_IsString(handler) && num_faults < MAX_FAULTS) {
                    fault_names[num_faults] = strdup(name->valuestring);
                    fault_addrs[num_faults] = strtoull(handler->valuestring, NULL, 0) & ~1ULL;
                    num_faults++;
                }
            }
        }
        
        cJSON *def_h = cJSON_GetObjectItemCaseSensitive(root, "default_handler");
        if (def_h && cJSON_IsString(def_h)) {
            default_handler_addr = strtoull(def_h->valuestring, NULL, 0) & ~1ULL;
        }

        cJSON *res_h = cJSON_GetObjectItemCaseSensitive(root, "reset_handler");
        if (res_h && cJSON_IsString(res_h)) {
            reset_handler_addr = strtoull(res_h->valuestring, NULL, 0) & ~1ULL;
        }

        cJSON *panics = cJSON_GetObjectItemCaseSensitive(root, "panics");
        if (panics && cJSON_IsArray(panics)) {
            cJSON *entry;
            cJSON_ArrayForEach(entry, panics) {
                if (cJSON_IsString(entry) && num_panics < MAX_PANICS) {
                    panic_addrs[num_panics] = strtoull(entry->valuestring, NULL, 0) & ~1ULL;
                    num_panics++;
                }
            }
        }

        cJSON *bounds = cJSON_GetObjectItemCaseSensitive(root, "valid_bounds");
        if (bounds && cJSON_IsArray(bounds)) {
            cJSON *entry;
            cJSON_ArrayForEach(entry, bounds) {
                cJSON *start = cJSON_GetObjectItemCaseSensitive(entry, "start");
                cJSON *end = cJSON_GetObjectItemCaseSensitive(entry, "end");
                if (cJSON_IsString(start) && cJSON_IsString(end) && num_bounds < MAX_BOUNDS) {
                    bounds_start[num_bounds] = strtoull(start->valuestring, NULL, 0);
                    bounds_end[num_bounds] = strtoull(end->valuestring, NULL, 0);
                    num_bounds++;
                }
            }
        }
    } else {
        // Fallback for old array format just in case
        if (root && cJSON_IsArray(root)) {
            cJSON *entry;
            cJSON_ArrayForEach(entry, root) {
                cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
                cJSON *handler = cJSON_GetObjectItemCaseSensitive(entry, "handler");
                if (cJSON_IsString(name) && cJSON_IsString(handler) && num_faults < MAX_FAULTS) {
                    fault_names[num_faults] = strdup(name->valuestring);
                    fault_addrs[num_faults] = strtoull(handler->valuestring, NULL, 0) & ~1ULL;
                    num_faults++;
                }
            }
        }
    }
    
    cJSON_Delete(root);
    free(json_data);
    
    
    if (milestones_json_path) {
        char *m_data = read_entire_file(milestones_json_path);
        if (m_data) {
            cJSON *m_root = cJSON_Parse(m_data);
            if (m_root && cJSON_IsObject(m_root)) {
                cJSON *m_arr = cJSON_GetObjectItemCaseSensitive(m_root, "milestones");
                if (m_arr && cJSON_IsArray(m_arr)) {
                    cJSON *entry;
                    cJSON_ArrayForEach(entry, m_arr) {
                        if (cJSON_IsNumber(entry) && num_milestones < MAX_MILESTONES) {
                            milestone_addrs[num_milestones] = ((uint64_t)entry->valuedouble) & ~1ULL;
                            num_milestones++;
                        }
                    }
                }
            }
            cJSON_Delete(m_root);
            free(m_data);
        }
    }
    printf("[Probe] Initialized. %d bounds, %d panics, %d faults, %d milestones.\n", num_bounds, num_panics, num_faults, num_milestones);
}

static void probe_check_sp(uint64_t pc) {
    uint64_t sp = core_get_sp();
    // Rough check: allow 0x20000000..0x30000000 (SRAM) and 0x10000000..0x11000000 (TCM)
    // Allow if SP is exactly 0x0 on early boot? Usually reset handler sets it.
    if (sp == 0) return; 
    if ((sp < 0x20000000 || sp >= 0x30000000) && (sp < 0x10000000 || sp >= 0x11000000)) {
        probe_exit_with_result("invalid_sp", pc, sp);
    }
}

void probe_check_read(uint64_t pc, uint64_t addr, uint64_t value) {
    probe_check_sp(pc);
    if (addr == last_mmio_read_addr && value == last_mmio_read_value) {
        reads_since_last_mmio_write++;
        if (reads_since_last_mmio_write >= PROBE_POLL_THRESHOLD) {
            probe_exit_with_result("peripheral_status_wait", pc, addr);
        }
    } else {
        last_mmio_read_addr = addr;
        last_mmio_read_value = value;
        reads_since_last_mmio_write = 1;
    }
}

void probe_check_write(uint64_t pc, uint64_t addr, uint64_t value) {
    probe_check_sp(pc);
    reads_since_last_mmio_write = 0;
}


void probe_check_unhandled_access(uint64_t pc, uint64_t addr, int is_write) {
    if (is_write) reads_since_last_mmio_write = 0;
    if (is_write) {
        probe_exit_with_result("unhandled_mmio_write", pc, addr);
    } else {
        probe_exit_with_result("unhandled_mmio_read", pc, addr);
    }
}

// Expose TB trans hook registration for core.c
void probe_register_hooks(qemu_plugin_id_t id) {
    qemu_plugin_register_vcpu_tb_trans_cb(id, probe_vcpu_tb_trans);
    core_register_exit_hook(probe_atexit);
}
