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
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <immintrin.h>
#include "core.h"
#include "common.h"

extern int coverage;

static bool fuzz_started = false;

static char g_fuzzing_buf[1024];
static char g_fuzzing_input[1024];

static uint32_t g_prev_pc;

// Dumping to fastdyn_work will cause this to be erased after each run, bad if fuzzing restarts
#define DEFAULT_BBL_DUMP_PATH "fuzz_out/bbl.txt"
static const char *bbl_dump_path = DEFAULT_BBL_DUMP_PATH;
static GHashTable *bbl_set;

// list of consecutive address+size values listing writable regions of memory, count is total entries not # of pairs
#define WLIST_PATH "fastdyn_work/bin-writable-ranges"
static size_t wlist_count = 0;
static uint32_t *wlist = NULL;

// If testing stability with forced inputs, this will cause it to compare all runs and output when a different path taken
static const bool forced_trace = false;
static GArray *current_run = NULL;
static GArray *previous_run = NULL;

#define MAP_SIZE 65536 // should always match the rust definition
extern uint8_t CVG[MAP_SIZE];
extern AddressList cc_list;

typedef void (*fuzz_anchor_callback_t)(char *buff, size_t len);
static fuzz_anchor_callback_t g_fuzz_anchor_callback = NULL;

typedef enum {
    FUZZ_EMPTY = 0, // buffer is ready for fuzzer to give an input
    FUZZ_READY = 1, // buffer is ready for anchor to read input
    FUZZ_BUSY = 2, // anchor as successfully read input
} fuzz_state_t;

typedef struct fuzz_input {
    size_t len;
    uint8_t *data;
} fuzz_input_t;

// Designed for a system where producer and consumer are each single threaded
typedef struct fuzz_buffer {
    _Atomic fuzz_state_t state;
    _Atomic uint32_t assert; // 0 = input didn't reach assert, 1 = assert, 2 = fatal assert
    fuzz_input_t *buffer;
} fuzz_buffer_t;

static fuzz_buffer_t fuzz_buffer;

void fuzz_register_callback(fuzz_anchor_callback_t cb) {
    g_fuzz_anchor_callback = cb;
}

static void fuzz_irq_entry(int irq) {
    cc_list.log_buf.buffer[(uint16_t)cc_list.log_buf.index / 4] = 0xFFFFFFFF;
    cc_list.log_buf.index = (uint16_t)(cc_list.log_buf.index + 4);
}
static void fuzz_irq_exit(int irq) {
    cc_list.log_buf.buffer[(uint16_t)cc_list.log_buf.index / 4] = 0xFFFFFFFD;
    cc_list.log_buf.index = (uint16_t)(cc_list.log_buf.index + 4);
}

// input side (Rust -> C)
bool fuzz_buffer_write(fuzz_input_t *input) {
    fuzz_state_t state = atomic_load_explicit(&fuzz_buffer.state, memory_order_acquire);
    if (state == FUZZ_EMPTY) {
        fuzz_buffer.buffer = input;
        atomic_store_explicit(&fuzz_buffer.state, FUZZ_READY, memory_order_release);
        return true;
    } else {
        fprintf(stderr, "[anchor] Fuzzer not empty\n");
        return false;
    }
}

// C function for retrieving input
static int fuzz_buffer_read(char* out, size_t len) {
    while (atomic_load_explicit(&fuzz_buffer.state, memory_order_acquire) != FUZZ_READY) {
        _mm_pause();
    }
    if (fuzz_buffer.buffer == NULL) {
        fprintf(stderr, "[anchor] Input is ready but buffer is null\n");
        return 0;
    }

    atomic_store_explicit(&fuzz_buffer.state, FUZZ_BUSY, memory_order_release); // clear assert flag
    atomic_store_explicit(&fuzz_buffer.assert, 0, memory_order_release); // clear assert flag

    memcpy(out, fuzz_buffer.buffer->data, len < fuzz_buffer.buffer->len ? len : fuzz_buffer.buffer->len);
    size_t input_size = fuzz_buffer.buffer->len;

    free(fuzz_buffer.buffer->data);
    free(fuzz_buffer.buffer);

    fuzz_buffer.buffer = NULL;

    return input_size;
}

bool fuzz_check_empty() {
    return (atomic_load_explicit(&fuzz_buffer.state, memory_order_relaxed) == FUZZ_EMPTY);
}

static void fuzz_trace_print_run(GArray *run, const char *name) {
    printf("%s (%u entries):\n", name, run->len);
    for (guint i = 0; i < run->len; i++) {
        uint32_t val = g_array_index(run, uint32_t, i);
        printf("  [%4u] 0x%08x\n", i, val);
    }
}

// for debugging on forcing the same input to view changed path, forced_trace = true
// This is setup at compile time as its just a somewhat target specific debugging helper
static void fuzz_trace_finish_run(void) {
    if (previous_run->len == 0) {
        printf("First run recorded.\n");
        g_array_append_vals(previous_run,
                            current_run->data,
                            current_run->len);
        g_array_set_size(current_run, 0);
        return;
    }

    gboolean identical = TRUE;
    guint min_len = MIN(previous_run->len, current_run->len);

    for (guint i = 0; i < min_len; i++) {
        uint32_t a = g_array_index(previous_run, uint32_t, i);
        uint32_t b = g_array_index(current_run, uint32_t, i);

        if (a != b) {
            printf("Runs diverge at index %u:\n", i);
            printf("  Previous: 0x%08x\n", a);
            printf("  Current : 0x%08x\n", b);
            identical = FALSE;
            break;
        }
    }

    if (identical && previous_run->len != current_run->len) {
        printf("Runs differ in length: prev=%u current=%u\n",
               previous_run->len,
               current_run->len);
        identical = FALSE;
    }

    if (!identical) {
        fuzz_trace_print_run(previous_run, "Previous run");
        fuzz_trace_print_run(current_run,  "Current run");
        exit(0);
    } else {
        printf("Run identical to previous.\n");
    }

    /* Replace previous with current */
    g_array_set_size(previous_run, 0);
    g_array_append_vals(previous_run,
                        current_run->data,
                        current_run->len);

    g_array_set_size(current_run, 0);
}

// If the anchor is busy (didn't skip consuming input), set it to done
static void fuzz_finish() {
    // Wait until tracer catches up with the logged pc's, otherwise coverage is innacurate
    uint16_t idx = (uint16_t)(cc_list.log_buf.index - 4);
    uint32_t last_log = cc_list.log_buf.buffer[idx / 4];

    while (g_prev_pc != last_log);

    g_prev_pc = 0;

    // first time hitting anchor, so clear coverage for pre-anchor tracked pcs
    if (fuzz_started == false) {
        if (forced_trace) {
            current_run  = g_array_new(FALSE, FALSE, sizeof(uint32_t));
            previous_run = g_array_new(FALSE, FALSE, sizeof(uint32_t));
        }

        memset(CVG, 0, sizeof(CVG));
        return;
    }

    if (forced_trace) fuzz_trace_finish_run();

    if (atomic_load_explicit(&fuzz_buffer.state, memory_order_acquire) == FUZZ_BUSY) {
        atomic_store_explicit(&fuzz_buffer.state, FUZZ_EMPTY, memory_order_release);
    }
}

uint32_t fuzz_check_assert() {
    uint32_t assrt = atomic_load_explicit(&fuzz_buffer.assert, memory_order_relaxed);
    return assrt;
}

static void fuzz_report_assert(bool fatal) {
    if (fatal) {
        atomic_store_explicit(&fuzz_buffer.assert, 2, memory_order_release);
    } else {
        atomic_store_explicit(&fuzz_buffer.assert, 1, memory_order_release);
    }
}

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

    fclose(f);

    *out_count = count;   // number of uint32_t entries (not pairs!)
    return array;
}

static void fuzz_initialize_anchor(char* args) {
    if (!args) {
        utils_die("Error, anchor doesn't have arguments");
    }

    atomic_init(&fuzz_buffer.state, FUZZ_EMPTY);
    atomic_init(&fuzz_buffer.assert, 0);
    fuzz_buffer.buffer = NULL;

    wlist = fuzz_get_writable_ranges(WLIST_PATH, &wlist_count);
    if (wlist == NULL || (wlist_count & 1)) {
        utils_die("[anchor] Couldn't parse writable memory definitions");
    }

    if (!fuzz_init(args)) {
        utils_die("[anchor] Failed initialization");
    }

    core_register_irq_hook(fuzz_irq_entry, fuzz_irq_exit);
}

void virt_assert(unsigned int cpu_index, void *udata)
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
        fuzz_report_assert(false);
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

        fuzz_report_assert(true);
        fuzz_finish();
        // wait for fuzzer to observe the crash
        while (true);
    }
}

void anchor(unsigned int cpu_index, void *udata)
{
    if (!coverage) {
        utils_die("[anchor] Coverage not enabled, cannot assert coverage data");
    }
    if (!udata) return;

    static uint32_t saved_regs[16];
    static uint8_t *membuff = NULL;

    // Lets libAFL know input is done, enforces correct coverage tracking timing
    fuzz_finish();
    if (fuzz_started) {
        // Restore saved regs
        for (int i = 0; i < 16; i++) {
            qemu_set_register(saved_regs[i], i);
        }

        // Restore saved memory
        uint32_t total_mem = 0;
        for (int i = 0; i < wlist_count; i += 2) {
            qemu_plugin_write_memory(wlist[i], membuff + total_mem, wlist[i+1]);
            total_mem += wlist[i+1];
        }
    } else {
        fuzz_initialize_anchor(udata);
        // save regs
        for (int i = 0; i < 16; i++) {
            saved_regs[i] = qemu_get_register(i);
        }

        uint32_t total_mem = 0;
        for (int i = 1; i < wlist_count; i += 2) {
            total_mem += wlist[i]; // pairs of addresses + size
        }
        
        membuff = malloc(total_mem);
        if (!membuff) {
            printf("Failed to malloc buffer\n");
            return;
        }

        // Save current memory
        total_mem = 0;
        for (int i = 0; i < wlist_count; i += 2) {
            qemu_plugin_read_memory(wlist[i], membuff + total_mem, wlist[i+1]);
            total_mem += wlist[i+1];
        }
        fuzz_started = true;
    }

    uint32_t read_count = fuzz_buffer_read(g_fuzzing_input, sizeof(g_fuzzing_input));

    // if callback registered, give callback new input
    if (g_fuzz_anchor_callback) {
        g_fuzz_anchor_callback(g_fuzzing_input, read_count);
    }

    // Parse each number
    // Fuzzes registers/memory from anchor if args supplied
    int idx = 0;
    char *token = strtok(udata, ",");
    while (token && (idx + 4) <= read_count) {
        unsigned long value = strtoul(token, NULL, 0);
        if (value < 100) {
            //vale < 100 means its a register number to write to
            uint32_t write_value = 0;
            memcpy(&write_value, g_fuzzing_input + idx, 4);
            //printf("Writing %x to %d at pc = %x, lr = %x\n", write_value, value, qemu_get_register(15), qemu_get_register(14));
            qemu_set_register(write_value, value);
        } else {
            qemu_plugin_write_memory(value, (uint8_t *)&g_fuzzing_input[idx], 4);
        }
        idx +=4;
        token = strtok(NULL, ",");
    }
}

static void fuzz_bbl_add(uint32_t pc) {
    guint bbl_pc = pc & (~1ULL);
    if (!g_hash_table_lookup(bbl_set, GUINT_TO_POINTER(bbl_pc))) {
        guint timestamp = (guint)(g_get_monotonic_time() / 1000);

        g_hash_table_insert(
            bbl_set,
            GUINT_TO_POINTER(bbl_pc),
            GUINT_TO_POINTER(timestamp)
        );
    }
}

void fuzz_dump_bbl(void)
{
    if (!bbl_set) {
        utils_die("bbl_set is not initialized");
    }

    FILE *f = fopen(bbl_dump_path, "w");
    if (!f) {
        utils_die("Failed to open dump file for writing");
    }

    GHashTableIter iter;
    gpointer key, value;
    int total = 0;

    g_hash_table_iter_init(&iter, bbl_set);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        guint pc = GPOINTER_TO_UINT(key);
        guint timestamp = GPOINTER_TO_UINT(value);

        /* Write as: hex_pc<TAB>timestamp */
        fprintf(f, "%08x\t%u\n", pc, timestamp);

        total++;
    }

    fclose(f);

    printf("Dumped %d blocks\n", total);
}


void fuzz_bbl_init(void)
{
    int total = 0;

    bbl_set = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!bbl_set) {
        utils_die("Failed to allocate bbl_set");
    }

    FILE *f = fopen(bbl_dump_path, "r");
    if (!f) {
        printf("Allocating new bbl_set\n");
        return;
    }

    guint pc;
    guint timestamp;

    /* Read lines like: 08001234<TAB>1573 */
    while (fscanf(f, "%x\t%u", &pc, &timestamp) == 2) {
        total++;

        g_hash_table_insert(
            bbl_set,
            GUINT_TO_POINTER(pc),
            GUINT_TO_POINTER(timestamp)
        );
    }

    fclose(f);

    printf("Loaded %d blocks\n", total);
}

void fuzz_add_observed_value(uint32_t val) {
    static int irq_depth = 0;
    
    if (val == 0xFFFFFFFF) {
        irq_depth++;
    } else if (val == 0xFFFFFFFD) {
        irq_depth--;
    } else {
        // we don't want stale value from before trace
        if (g_prev_pc != 0) {
            uint32_t idx = (g_prev_pc ^ val) % MAP_SIZE;
            CVG[idx] = CVG[idx] + 1;
        }

        //printf("[trace] %x\n", val);
        if (current_run) g_array_append_val(current_run, val);
        g_prev_pc = val;
    }
    fuzz_bbl_add(val);
}

int fuzz_init(int argc, char **argv) {

}
