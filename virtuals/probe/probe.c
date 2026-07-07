#include <probe.h>

#include <core.h>
#include <inspct.h>
#include <inttypes.h>
#include <qemu/qemu-plugin.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <utils.h>

#include "cJSON.h"

extern uint32_t qemu_get_register(int reg);
extern int qemu_plugin_read_memory(unsigned long long addr, uint8_t *mem_buf, int len);

#define PROBE_POLL_THRESHOLD 1000

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

#define MAX_MILESTONES 50
static uint64_t milestone_addrs[MAX_MILESTONES];
static int num_milestones = 0;

#define MAX_IGNORES 20
static uint64_t ignore_addrs[MAX_IGNORES];
static int num_ignores = 0;
static uint64_t ignored_execution_count = 0;
#define MAX_IGNORED_BBLS 1000000

#define BBL_TRACE_SIZE 10000
static uint64_t bbl_trace_pc[BBL_TRACE_SIZE];
static uint32_t bbl_trace_count[BBL_TRACE_SIZE];
static int bbl_idx = 0;

static uint64_t total_bbl_executed = 0;
static uint64_t unique_bbl_executed = 0;

#define BBL_UNIQUE_SET_SIZE 262144
static uint64_t bbl_unique_set[BBL_UNIQUE_SET_SIZE];

static uint64_t instruction_count = 0;
static uint64_t last_novel_instruction_count = 0;
#define NO_NEW_COVERAGE_THRESHOLD 50000000

static uint64_t last_mmio_read_addr = 0;
static uint64_t last_mmio_read_value = 0;
static int reads_since_last_mmio_write = 0;

static char probe_out_dir[512] = ".";
static bool probe_enabled = false;
static bool probe_result_written = false;

bool probe_is_enabled(void)
{
    return probe_enabled;
}

static void probe_track_unique_bbl(uint64_t pc)
{
    uint64_t key = pc | 1ULL;
    uint64_t mixed = key ^ (key >> 33);
    mixed *= 0xff51afd7ed558ccdULL;
    mixed ^= (mixed >> 33);

    size_t idx = (size_t)(mixed & (BBL_UNIQUE_SET_SIZE - 1));
    for (size_t i = 0; i < BBL_UNIQUE_SET_SIZE; i++) {
        uint64_t existing = bbl_unique_set[idx];
        if (existing == key) {
            return;
        }
        if (existing == 0) {
            bbl_unique_set[idx] = key;
            unique_bbl_executed++;
            return;
        }
        idx = (idx + 1) & (BBL_UNIQUE_SET_SIZE - 1);
    }
}

static char *probe_read_entire_file(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc((size_t)length + 1);
    if (data) {
        size_t read_len = fread(data, 1, (size_t)length, f);
        data[read_len] = '\0';
    }
    fclose(f);
    return data;
}

static void probe_write_result(const char *reason, uint64_t pc, uint64_t extra_info)
{
    if (probe_result_written) {
        return;
    }
    probe_result_written = true;

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/probe_result.json", probe_out_dir);

    FILE *f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "{\n");
        fprintf(f, "  \"exit_reason\": \"%s\",\n", reason);
        fprintf(f, "  \"pc\": \"0x%08" PRIx64 "\",\n", pc);
        fprintf(f, "  \"extra_info\": \"0x%08" PRIx64 "\",\n", extra_info);

        if (strcmp(reason, "panic_or_assert") == 0) {
            uint32_t r0 = qemu_get_register(0);
            char panic_str[64] = {0};
            qemu_plugin_read_memory(r0, (uint8_t *)panic_str, sizeof(panic_str) - 1);
            for (int i = 0; i < 63; i++) {
                if (panic_str[i] == '\n' || panic_str[i] == '\r') {
                    panic_str[i] = ' ';
                }
                if (panic_str[i] == '"') {
                    panic_str[i] = '\'';
                }
                if (panic_str[i] < 32 && panic_str[i] != 0) {
                    panic_str[i] = '?';
                }
            }
            fprintf(f, "  \"panic_string\": \"%s\",\n", panic_str);
        } else if (strstr(reason, "Fault") != NULL || strstr(reason, "fault") != NULL) {
            uint32_t sp = qemu_get_register(13);
            uint32_t stacked_pc = 0;
            qemu_plugin_read_memory(sp + 24, (uint8_t *)&stacked_pc, 4);
            uint32_t cfsr = 0;
            uint32_t bfar = 0;
            qemu_plugin_read_memory(0xE000ED28, (uint8_t *)&cfsr, 4);
            qemu_plugin_read_memory(0xE000ED38, (uint8_t *)&bfar, 4);
            fprintf(f, "  \"stacked_pc\": \"0x%08X\",\n", stacked_pc);
            fprintf(f, "  \"cfsr\": \"0x%08X\",\n", cfsr);
            fprintf(f, "  \"bfar\": \"0x%08X\",\n", bfar);
        } else if (strcmp(reason, "no_new_coverage") == 0) {
            fprintf(f, "  \"registers\": {\n");
            for (int r = 0; r < 13; r++) {
                fprintf(f, "    \"r%d\": \"0x%08X\"%s\n", r, qemu_get_register(r), (r == 12) ? "" : ",");
            }
            fprintf(f, "  },\n");
        }

        if (inspct_write_probe_json(f, "  ")) {
            fprintf(f, ",\n");
        }

        int retained_bbl_trace_entries = (bbl_idx < BBL_TRACE_SIZE) ? bbl_idx : BBL_TRACE_SIZE;
        uint64_t retained_bbl_trace_executed = 0;
        int retained_bbl_trace_unique = 0;
        int start = (bbl_idx < BBL_TRACE_SIZE) ? 0 : (bbl_idx % BBL_TRACE_SIZE);
        for (int i = 0; i < retained_bbl_trace_entries; i++) {
            int idx = (start + i) % BBL_TRACE_SIZE;
            retained_bbl_trace_executed += bbl_trace_count[idx];
            bool seen = false;
            for (int j = 0; j < i; j++) {
                if (bbl_trace_pc[idx] == bbl_trace_pc[(start + j) % BBL_TRACE_SIZE]) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                retained_bbl_trace_unique++;
            }
        }

        fprintf(f, "  \"total_bbl_executed\": %" PRIu64 ",\n", total_bbl_executed);
        fprintf(f, "  \"unique_bbl_executed\": %" PRIu64 ",\n", unique_bbl_executed);
        fprintf(f, "  \"retained_bbl_trace_entries\": %d,\n", retained_bbl_trace_entries);
        fprintf(f, "  \"retained_bbl_trace_executed\": %" PRIu64 ",\n", retained_bbl_trace_executed);
        fprintf(f, "  \"retained_bbl_trace_unique\": %d,\n", retained_bbl_trace_unique);
        fprintf(f, "  \"bbl_trace\": [\n");
        for (int i = 0; i < retained_bbl_trace_entries; i++) {
            fprintf(f, "    {\"pc\": \"0x%08" PRIx64 "\", \"count\": %u}%s\n",
                    bbl_trace_pc[(start + i) % BBL_TRACE_SIZE],
                    bbl_trace_count[(start + i) % BBL_TRACE_SIZE],
                    (i == retained_bbl_trace_entries - 1) ? "" : ",");
        }
        fprintf(f, "  ]\n");
        fprintf(f, "}\n");
        fclose(f);
    } else {
        fprintf(stderr, "[Probe] Failed to write %s\n", filepath);
    }

    printf("[Probe] Exiting due to %s at PC 0x%08" PRIx64 "\n", reason, pc);
}

static void probe_exit_with_result(const char *reason, uint64_t pc, uint64_t extra_info)
{
    probe_write_result(reason, pc, extra_info);
    exit(0);
}

static void probe_atexit(void)
{
    probe_write_result("manual_stop", 0, 0);
}

static void probe_vcpu_tb_exec(unsigned int vcpu_index, void *userdata)
{
    (void)vcpu_index;
    uint64_t pc = (uint64_t)userdata;

    total_bbl_executed++;
    probe_track_unique_bbl(pc);

    if (bbl_idx > 0) {
        int last_idx = (bbl_idx - 1) % BBL_TRACE_SIZE;
        if (bbl_trace_pc[last_idx] == pc) {
            bbl_trace_count[last_idx]++;
        } else {
            int new_idx = bbl_idx % BBL_TRACE_SIZE;
            bbl_trace_pc[new_idx] = pc;
            bbl_trace_count[new_idx] = 1;
            bbl_idx++;
        }
    } else {
        bbl_trace_pc[0] = pc;
        bbl_trace_count[0] = 1;
        bbl_idx++;
    }

    int is_ignored = 0;
    for (int i = 0; i < num_ignores; i++) {
        if (pc == ignore_addrs[i]) {
            is_ignored = 1;
            break;
        }
    }

    if (is_ignored) {
        ignored_execution_count++;
        last_novel_instruction_count = instruction_count;

        if (ignored_execution_count > MAX_IGNORED_BBLS) {
            probe_exit_with_result("idle_starvation", pc, 0);
        }
    } else {
        ignored_execution_count = 0;
        instruction_count++;

        if (instruction_count - last_novel_instruction_count > NO_NEW_COVERAGE_THRESHOLD) {
            probe_exit_with_result("no_new_coverage", pc, 0);
        }
    }
}

static void probe_wfi_exec(unsigned int vcpu_index, void *userdata)
{
    (void)vcpu_index;
    uint64_t pc = (uint64_t)userdata;
    probe_exit_with_result("wfi_idle_starvation", pc, 0);
}

static void probe_vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    (void)id;
    uint64_t pc = qemu_plugin_tb_vaddr(tb);

    qemu_plugin_register_vcpu_tb_exec_cb(tb, probe_vcpu_tb_exec, QEMU_PLUGIN_CB_NO_REGS, (void *)pc);

    last_novel_instruction_count = instruction_count;

    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        if (qemu_plugin_insn_size(insn) == 2) {
            uint8_t data[2];
            if (qemu_plugin_insn_data(insn, data, 2) == 2) {
                if (data[0] == 0x30 && data[1] == 0xbf) {
                    qemu_plugin_register_vcpu_insn_exec_cb(
                        insn,
                        probe_wfi_exec,
                        QEMU_PLUGIN_CB_NO_REGS,
                        (void *)qemu_plugin_insn_vaddr(insn));
                }
            }
        }
    }

    for (int i = 0; i < num_milestones; i++) {
        if (pc == milestone_addrs[i]) {
            probe_exit_with_result("milestone_reached", pc, 0);
        }
    }

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
            printf("[Probe] Hit fault handler: %s\n", fault_names[i]);
            probe_exit_with_result(fault_names[i], pc, 0);
        }
    }
}

static void probe_load_config(
    const char *faults_json_path,
    const char *milestones_json_path,
    const char *out_dir,
    const char *ignores_json_path)
{
    if (out_dir) {
        strncpy(probe_out_dir, out_dir, sizeof(probe_out_dir) - 1);
        probe_out_dir[sizeof(probe_out_dir) - 1] = '\0';
    }

    if (!faults_json_path) {
        return;
    }

    char *json_data = probe_read_entire_file(faults_json_path);
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
    } else if (root && cJSON_IsArray(root)) {
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

    cJSON_Delete(root);
    free(json_data);

    if (milestones_json_path) {
        char *m_data = probe_read_entire_file(milestones_json_path);
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

    if (ignores_json_path) {
        char *i_data = probe_read_entire_file(ignores_json_path);
        if (i_data) {
            cJSON *i_root = cJSON_Parse(i_data);
            if (i_root && cJSON_IsObject(i_root)) {
                cJSON *i_arr = cJSON_GetObjectItemCaseSensitive(i_root, "ignores");
                if (i_arr && cJSON_IsArray(i_arr)) {
                    cJSON *entry;
                    cJSON_ArrayForEach(entry, i_arr) {
                        if (cJSON_IsNumber(entry) && num_ignores < MAX_IGNORES) {
                            ignore_addrs[num_ignores] = ((uint64_t)entry->valuedouble) & ~1ULL;
                            num_ignores++;
                        }
                    }
                }
            }
            cJSON_Delete(i_root);
            free(i_data);
        }
    }

    printf("[Probe] Initialized. %d bounds, %d panics, %d faults, %d milestones, %d ignores.\n",
           num_bounds, num_panics, num_faults, num_milestones, num_ignores);
}

static int probe_arg_enabled(const char *probe)
{
    if (!probe) {
        return 0;
    }

    if (!strcasecmp(probe, "true") || !strcasecmp(probe, "on") || !strcmp(probe, "1")) {
        return 1;
    }
    if (!strcasecmp(probe, "false") || !strcasecmp(probe, "off") || !strcmp(probe, "0")) {
        return 0;
    }

    fprintf(stderr, "fastdyn: unknown probe_run mode: '%s'\n", probe);
    utils_die("bad probe_run mode");
    return 0;
}

int probe_init(int argc, char **argv)
{
    probe_enabled = probe_arg_enabled(utils_get_arg("probe_run", argc, argv));
    if (!probe_enabled) {
        return 0;
    }

    const char *faults = utils_get_arg("probe_faults", argc, argv);
    const char *milestones = utils_get_arg("probe_milestones", argc, argv);
    const char *ignores = utils_get_arg("probe_ignores", argc, argv);
    const char *out_dir = utils_get_arg("probe_out_dir", argc, argv);

    probe_load_config(faults, milestones, out_dir, ignores);
    core_register_tb_trans_hook(probe_vcpu_tb_trans);
    core_register_exit_hook(probe_atexit);
    return 0;
}

static void probe_check_sp(uint64_t pc)
{
    uint64_t sp = core_get_sp();
    if (sp == 0) {
        return;
    }
    if ((sp < 0x20000000 || sp >= 0x30000000) && (sp < 0x10000000 || sp >= 0x11000000)) {
        probe_exit_with_result("invalid_sp", pc, sp);
    }
}

void probe_check_read(uint64_t pc, uint64_t addr, uint64_t value)
{
    if (!probe_enabled) {
        return;
    }

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

void probe_check_write(uint64_t pc, uint64_t addr, uint64_t value)
{
    (void)addr;
    (void)value;
    if (!probe_enabled) {
        return;
    }

    probe_check_sp(pc);
    reads_since_last_mmio_write = 0;
}

void probe_check_unhandled_access(uint64_t pc, uint64_t addr, int is_write)
{
    if (!probe_enabled) {
        return;
    }

    if (is_write) {
        reads_since_last_mmio_write = 0;
        probe_exit_with_result("unhandled_mmio_write", pc, addr);
    } else {
        probe_exit_with_result("unhandled_mmio_read", pc, addr);
    }
}
