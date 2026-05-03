#define _GNU_SOURCE
#include <dlfcn.h>
#include <utils.h>
#include <core.h>
#include <common.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <immintrin.h>

#include "virtuals.h"
#include "fuzz.h"
#include "cov_bbl.h"
#include "cov_trace.h"

#include "protocol_fuzzers/protocol_fuzzers.h"
#include "stateful_fuzzers/stateful_fuzzers.h"

static int coverage = 0;

static char g_fuzzing_buf[1024];

static uint32_t g_prev_pc;

#define CVG_PATH "fastdyn_work/cvg.bin"
#define SNAPSHOT_RAW_PATH "fastdyn_work/snapshot.bin"
#define SNAPSHOT_REG_COUNT 16

// list of consecutive address+size values listing writable regions of memory, count is total entries not # of pairs
static const char *writable_ranges_path(void) {
    const char *path = getenv("FASTDYN_WRITABLE_RANGES_FILE");
    if (path && path[0]) {
        return path;
    }

    const char *work_dir = getenv("FASTDYN_WORK_DIR");
    if (work_dir && work_dir[0]) {
        static char buffer[4096];
        snprintf(buffer, sizeof(buffer), "%s/bin-writable-ranges", work_dir);
        return buffer;
    }

    return "fastdyn_work/bin-writable-ranges";
}
static size_t wlist_count = 0;
static uint32_t *wlist = NULL;
static uint32_t g_stack_top = 0;

static uint8_t *snap_membuff = NULL;
static uint32_t g_snapshot_regs[SNAPSHOT_REG_COUNT];
static bool g_snapshot_loaded_from_file = false;

typedef enum {
    FUZZ_MSG_INACTIVE = 0,
    FUZZ_MSG_READY,
    FUZZ_MSG_CONSUMING,
    FUZZ_MSG_CONSUMED,
} fuzz_msg_state_t;

static _Atomic int g_active_msg_state = FUZZ_MSG_INACTIVE;
static const uint8_t *g_active_msg_data = NULL;
static size_t g_active_msg_len = 0;

extern bool g_trace_enabled;

extern uint8_t CVG[MAP_SIZE];
extern AddressList core_cc_list; // in future, update so that we aren't using an extern global for this, maybe add core.c helper to consume inputs

typedef void (*fuzz_callback_t)();
static fuzz_callback_t g_fuzz_sync_callback = NULL;
static fuzz_callback_t g_fuzz_exit_callback = NULL;
static fuzz_callback_t g_fuzz_restore_callback = NULL;

void fuzz_register_callback(fuzz_callback_t cb) {
    g_fuzz_sync_callback = cb;
}

void fuzz_register_exit(fuzz_callback_t cb) {
    g_fuzz_exit_callback = cb;
}

void fuzz_register_restore(fuzz_callback_t cb) {
    g_fuzz_restore_callback = cb;
}

// method could be better, but since we do this in a hook the firmware isn't executing and is safe
static void fuzz_irq_entry(int irq) {
    core_cc_list.log_buf.buffer[(uint16_t)core_cc_list.log_buf.index / 4] = 0xFFFFFFFF;
    core_cc_list.log_buf.index = (uint16_t)(core_cc_list.log_buf.index + 4);
}
static void fuzz_irq_exit(int irq) {
    core_cc_list.log_buf.buffer[(uint16_t)core_cc_list.log_buf.index / 4] = 0xFFFFFFFD;
    core_cc_list.log_buf.index = (uint16_t)(core_cc_list.log_buf.index + 4);
}

// wait for tracer in core to catch up with inline pc logger
static void fuzz_sync_coverage(void) {
    core_wait_for_trace_drain();
    g_prev_pc = 0;
}

// ----------------------- data getting & setting -----------------------------

// wait if handle is consuming, raturn state
static int fuzz_active_message_state(void)
{
    int state;

    do {
        state = atomic_load_explicit(&g_active_msg_state, memory_order_acquire);
        if (state == FUZZ_MSG_CONSUMING) {
            _mm_pause();
        }
    } while (state == FUZZ_MSG_CONSUMING);

    return state;
}

static fuzz_msg_state_t fuzz_deactivate_message(void)
{
    (void)fuzz_active_message_state();
    //g_active_msg_data = NULL;
    //g_active_msg_len = 0;
    return atomic_exchange_explicit(&g_active_msg_state, FUZZ_MSG_INACTIVE, memory_order_acq_rel);
}

static void fuzz_set_message_state(fuzz_msg_state_t state)
{
    (void)fuzz_active_message_state();
    atomic_store_explicit(&g_active_msg_state, state, memory_order_release);
}

static void fuzz_activate_message(const fuzz_backend_msg_t *msg)
{
    (void)fuzz_active_message_state();
    g_active_msg_data = msg->data;
    g_active_msg_len = msg->len;
    atomic_store_explicit(&g_active_msg_state, FUZZ_MSG_READY, memory_order_release);
}

size_t fuzz_get_data(char* buf, size_t len)
{
    int expected = FUZZ_MSG_READY;

    if (buf == NULL || len == 0) {
        return 0;
    }

    // essentially an atomic 'if (g_active_msg_state == expected) {g_active_msg_state = FUZZ_MSG_CONSUMING} else return 0'
    if (!atomic_compare_exchange_strong_explicit(
            &g_active_msg_state,
            &expected,
            FUZZ_MSG_CONSUMING,
            memory_order_acq_rel,
            memory_order_acquire)) {
        return 0;
    }

    size_t copy_len = g_active_msg_len < len ? g_active_msg_len : len;
    if (copy_len != 0 && g_active_msg_data == NULL) {
        copy_len = 0;
    }

    if (copy_len != 0) {
        memcpy(buf, g_active_msg_data, copy_len);
    }

    atomic_store_explicit(&g_active_msg_state, FUZZ_MSG_CONSUMED, memory_order_release);
    return copy_len;
}

void fuzz_set_data(char* buf, size_t len)
{
    fuzz_backend_set_data((const uint8_t *)buf, len);
}

// ------------------- snapshotting related functions ------------------------

// load the writable memory from bin-writable-ranges, produced by binary_wrange.py
// notably, this is not always 100% correct in all cases, where manual edits to bin-writable-ranges can improve snapshots
static uint32_t *fuzz_get_writable_ranges(const char *filename, size_t *out_count)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        return NULL;
    }

    size_t capacity = 16;          // initial number of uint32_t entries
    size_t count = 0;
    uint32_t *array = malloc(capacity * sizeof(uint32_t));
    if (!array) {
        fclose(f);
        return NULL;
    }

    uint32_t a, b;

    while (fscanf(f, "%x\t%x", &a, &b) == 2) {
        if (count + 2 > capacity) {
            capacity *= 2;
            uint32_t *tmp = realloc(array, capacity * sizeof(uint32_t));
            if (!tmp) {
                free(array);
                fclose(f);
                return NULL;
            }
            array = tmp;
        }

        printf("Adding %x size %x\n", a, b);

        array[count++] = a;
        array[count++] = b;
    }

    // Doesn't include stack information
    if (array[count-1] != 0) {
        printf("No stack information\n");
        return 0;
    }

    fclose(f);

    *out_count = count;   // number of uint32_t entries (not pairs!)
    return array;
}

static size_t fuzz_snapshot_size(void)
{
    size_t total_mem = 0;

    for (size_t i = 1; i < wlist_count; i += 2) {
        total_mem += wlist[i];
    }

    return total_mem;
}

static bool fuzz_snapshot_sp_valid(uint32_t sp)
{
    return g_stack_top != 0 && sp != 0 && sp <= g_stack_top;
}

static void fuzz_set_snapshot_sp(uint32_t sp)
{
    wlist[wlist_count - 2] = sp;
    wlist[wlist_count - 1] = g_stack_top - sp;
}

// take the initial snapshot, supports saving/loading snapshots to/from disk
int fuzz_snap_memory() {
    uint32_t file_regs[SNAPSHOT_REG_COUNT];
    uint32_t sp = qemu_get_register(13);
    size_t total_mem;
    FILE *f = fopen(SNAPSHOT_RAW_PATH, "rb");
    g_snapshot_loaded_from_file = false;

    // read an existing snapshot file if present, useful for skipping initialization, checks that sp is valid
    if (f) {
        if (fread(file_regs, sizeof(file_regs[0]), SNAPSHOT_REG_COUNT, f) == SNAPSHOT_REG_COUNT &&
            fuzz_snapshot_sp_valid(file_regs[13])) {
            fuzz_set_snapshot_sp(file_regs[13]);
            total_mem = fuzz_snapshot_size();

            snap_membuff = malloc(total_mem);
            if (!snap_membuff) {
                fclose(f);
                printf("Failed to malloc buffer\n");
                return -1;
            }

            if (fread(snap_membuff, 1, total_mem, f) == total_mem && fgetc(f) == EOF) {
                memcpy(g_snapshot_regs, file_regs, sizeof(g_snapshot_regs));
                g_snapshot_loaded_from_file = true;
                fclose(f);
                printf("Loaded snapshot from %s\n", SNAPSHOT_RAW_PATH);
                return 0;
            }

            free(snap_membuff);
            snap_membuff = NULL;
        }

        fclose(f);
        printf("Ignoring invalid snapshot dump at %s\n", SNAPSHOT_RAW_PATH);
    }

    fuzz_set_snapshot_sp(sp);
    total_mem = fuzz_snapshot_size();

    snap_membuff = malloc(total_mem);
    if (!snap_membuff) {
        printf("Failed to malloc buffer\n");
        return -1;
    }

    for (int i = 0; i < SNAPSHOT_REG_COUNT; i++) {
        g_snapshot_regs[i] = qemu_get_register(i);
    }

    // Save current memory
    size_t offset = 0;
    for (size_t i = 0; i < wlist_count; i += 2) {
        qemu_plugin_read_memory(wlist[i], snap_membuff + offset, wlist[i+1]);
        offset += wlist[i+1];
    }

    // dump snapshot to file
    f = fopen(SNAPSHOT_RAW_PATH, "wb");
    if (f) {
        if (fwrite(g_snapshot_regs, sizeof(g_snapshot_regs[0]), SNAPSHOT_REG_COUNT, f) != SNAPSHOT_REG_COUNT) {
            printf("Failed to write snapshot register header to %s\n", SNAPSHOT_RAW_PATH);
        } else if (fwrite(snap_membuff, 1, total_mem, f) != total_mem) {
            printf("Failed to write full snapshot to %s\n", SNAPSHOT_RAW_PATH);
        } else {
            printf("Wrote snapshot to %s\n", SNAPSHOT_RAW_PATH);
        }
        fclose(f);
    } else {
        printf("Failed to open %s for snapshot dump\n", SNAPSHOT_RAW_PATH);
    }

    return 0;
}

static bool g_snapshot_taken = false;

static bool fuzz_ensure_snapshot(void)
{
    if (g_snapshot_taken) {
        return true;
    }

    if (fuzz_snap_memory() != 0) {
        perror("Failed to take initial snapshot\n");
        return false;
    }

    g_snapshot_taken = true;
    return true;
}

static bool fuzz_restore_snapshot(void)
{
    if (!fuzz_ensure_snapshot() || !snap_membuff) {
        return false;
    }

    size_t total_mem = 0;
    for (size_t i = 0; i < wlist_count; i += 2) {
        qemu_plugin_write_memory(wlist[i], snap_membuff + total_mem, wlist[i+1]);
        total_mem += wlist[i+1];
    }

    // include pc, we may restore from a different point than taken
    for (int i = 0; i < 15; i++) {
        fuzz_set_register(g_snapshot_regs[i], i);
    }

    if (g_fuzz_restore_callback) {
        g_fuzz_restore_callback();
    }

    fuzz_trace_commit_run();
    fuzz_trace_compare();

    return true;
}

// ------------ assert and sync virtuals ----------------

static void virt_assert(unsigned int cpu_index, void *udata)
{
    if (!coverage) {
        utils_die("[assert] Coverage not enabled, cannot assert coverage data");
    }

    if (!udata)
        return;

    printf("[assert] Asserted\n");

    const char *str = (const char *)udata;

    // Expect something like "*0x8003940"
    if (str[0] != '*') {
        fprintf(stderr, "[assert] Invalid format: %s\n", str);
        return;
    }

    // Skip the '*' and parse the rest as hex or decimal
    uint64_t addr = strtoull(str + 1, NULL, 0);
    if (addr != 0) { // set pc to supplied address
        fuzz_backend_report_assert(false);
        qemu_set_register(addr, ARM_V7M_PC);
    } else { // for address == 0, perform a reset
        uint32_t msp = qemu_get_register(13);

        if (msp != 0) {
            qemu_plugin_read_memory(msp, (uint8_t*)g_fuzzing_buf, sizeof(uint32_t) * 8);

            printf("[assert] Hard fault:\n");
            for (int i = 0; i < 8; i++) {
                printf("[assert] Stack[%d] = %x\n", i, ((uint32_t*)g_fuzzing_buf)[i]);
            }
            for (int i = 0; i < 16; i++) {
                printf("[assert] r%d = %x\n", i, (uint32_t)qemu_get_register(i));
            }
        }

        fuzz_sync_coverage();
        fuzz_trace_commit_run();
        fuzz_trace_compare();
        fuzz_backend_report_assert(true);

        // wait for fuzzer to observe the crash
        while (true);
    }
}

static inline void observed_clear() {
    g_prev_pc = 0;
    memset(CVG, 0, sizeof(CVG));
}

static void fuzz_sync_point(unsigned int cpu_index, void *udata);
static void fuzz_snap_point(unsigned int cpu_index, void *udata)
{
    static bool initialized = false;

    if (!initialized) {
        if (!fuzz_ensure_snapshot()) {
            utils_die("[sync] Failed to take initial snapshot");
        }

        if (g_snapshot_loaded_from_file && !fuzz_restore_snapshot()) {
            utils_die("[sync] Failed to restore loaded snapshot");
        }

        fuzz_trace_reset();

        observed_clear();

        initialized = true;

        fuzz_sync_point(cpu_index, udata);
    }
}

static void fuzz_sync_point(unsigned int cpu_index, void *udata)
{
    if (!coverage) {
        utils_die("[fuzz_sync] Coverage not enabled, cannot assert coverage data");
    }
    if (!udata) return;

    static bool clear_next = false;

    fuzz_msg_state_t oldState = fuzz_deactivate_message();

    fuzz_sync_coverage();

    if (g_fuzz_sync_callback) {
        g_fuzz_sync_callback();
    }

    // not every response is input producing
    while (true) {
        if (oldState == FUZZ_MSG_READY) {
            perror("[fuzz_sync] Message is ready but we are at input completion, improve sync point\n");
            fuzz_set_message_state(oldState); // reactivate the message
            return;
        }

        fuzz_backend_msg_t msg = {0};
        if (!fuzz_backend_next(&msg)) {
            utils_die("[fuzz_sync] Backend failed to produce input");
        }

        if (msg.restore) {
            if (!fuzz_restore_snapshot()) {
                utils_die("[fuzz_sync] Failed to restore snapshot");
            }

            clear_next = true;

            fuzz_backend_restore_complete();
        }

        // fuzzer can request just a restore without a message
        if (msg.data) {
            // start message clean
            if (clear_next) {
                observed_clear();
                clear_next = false;
            }

            fuzz_activate_message(&msg);
            break;
        }
    }
}

uint32_t fuzz_get_register(int reg) {
    return qemu_get_register(reg);
}
void fuzz_set_register(uint32_t value, int reg) {
    qemu_set_register(value, reg);
}

int fuzz_write_memory(unsigned long long addr, uint8_t *mem_buf, int len) {
    return qemu_plugin_write_memory(addr, mem_buf, len);
}
int fuzz_read_memory(unsigned long long addr, uint8_t *mem_buf, int len) {
    return qemu_plugin_read_memory(addr, mem_buf, len);
}

void fuzz_add_observed_value(uint32_t val) {
    static int irq_depth = 0;
    
    if (val == 0xFFFFFFFF) {
        irq_depth++;
    } else if (val == 0xFFFFFFFD) {
        irq_depth--;
    } else {
        if (irq_depth == 0) {
            // we don't want stale value from before trace
            if (g_prev_pc != 0) {
                uint32_t idx = (g_prev_pc ^ val) % MAP_SIZE;
                CVG[idx] = CVG[idx] + 1;
            }

            if (g_trace_enabled) fuzz_trace_record_pc(val);
            g_prev_pc = val;
        }
        fuzz_bbl_add(val);
    }
}

static void fuzz_serialize_coverage(const char *filename) {
    char tmp[512];

    /* Construct temp filename in same directory */
    snprintf(tmp, sizeof(tmp), "%s.tmp", filename);

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        perror("[-] Failed to open temp coverage file for writing");
        return;
    }

    // comma separated values
    for (size_t i = 0; i < MAP_SIZE; i++) {
        if (fprintf(f, "%u", CVG[i]) < 0) {
            perror("[-] Write error");
            fclose(f);
            remove(tmp);
            return;
        }
        if (i < MAP_SIZE - 1) {
            fputc(',', f);
        }
    }
    fputc('\n', f);

    /* Ensure data is flushed to disk before rename */
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    /* Atomic rename replaces old file */
    if (rename(tmp, filename) != 0) {
        perror("[-] Failed to rename temp coverage file");
        remove(tmp);
    }
}

static void fuzz_destroy(void) {
    if (g_fuzz_exit_callback) {
        g_fuzz_exit_callback();
    }
    fuzz_dump_bbl();
    fuzz_serialize_coverage(coverage_output_path());
}

// miscelanious virtual you can use to test if the firmware reaches something without enabling gdb and tracing
static void fuzz_print_test(unsigned int cpu_index, void *udata) {
    printf("[test]\n");
}

int fuzz_init(int argc, char **argv) {
    const char *filename = utils_get_arg("coverage", argc, argv);
    if (filename &&
        (strcasecmp(filename, "true") == 0 || strcmp(filename, "1") == 0)) {

        coverage = 1;
        fuzz_bbl_init();
        core_register_exit_hook(fuzz_destroy);
    }

    core_register_irq_hook(fuzz_irq_entry, fuzz_irq_exit);

    wlist = fuzz_get_writable_ranges(WLIST_PATH, &wlist_count);

    if (wlist == NULL || (wlist_count & 1)) {
        utils_die("[sync] Couldn't parse writable memory definitions");
    }
    g_stack_top = wlist[wlist_count - 2];

    fuzz_deactivate_message();

    virtual_register("assert", virt_assert);
    virtual_register("fuzz_snap_point", fuzz_snap_point);
    virtual_register("fuzz_sync_point", fuzz_sync_point);

    // ---------- mqtt fuzzing virtuals ------------
    // virtual_register("fuzz_mqtt_in", fuzz_mqtt_in);

    // ---------- lwip fuzzing virtuals ------------
    // virtual_register("fuzz_eth_in", fuzz_eth_in);
    // virtual_register("fuzz_eth_out", fuzz_eth_out);
    // virtual_register("fuzz_pbuf_free", fuzz_pbuf_free);

    if (!fuzz_backend_init()) {
        utils_die("[sync] Failed backend initialization");
    }

    return 0;
}
