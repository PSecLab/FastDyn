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
#include "virtuals.h"
#include "fuzz_bbl.h"
#include "fuzz_trace.h"
#include "protocol_fuzzers/protocol_fuzzers.h"

static int coverage = 0;

static bool fuzz_started = false;

static char g_fuzzing_buf[1024];
static char g_fuzzing_input[1024];

static uint32_t g_prev_pc;

#define CVG_PATH "fastdyn_work/cvg.bin"

// list of consecutive address+size values listing writable regions of memory, count is total entries not # of pairs
#define WLIST_PATH "fastdyn_work/bin-writable-ranges"
static size_t wlist_count = 0;
static uint32_t *wlist = NULL;

// If testing stability with forced inputs, this will cause it to compare all runs and output when a different path taken
static const bool forced_trace = false;

#define MAP_SIZE 65536 // should always match the rust definition
extern uint8_t CVG[MAP_SIZE];
extern AddressList core_cc_list; // in future, update so that we aren't using an extern global for this, maybe add core.c helper to consume inputs
extern LoggerEntry core_cc_entry;
extern LookupResult core_cc_ret;

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
    core_cc_list.log_buf.buffer[(uint16_t)core_cc_list.log_buf.index / 4] = 0xFFFFFFFF;
    core_cc_list.log_buf.index = (uint16_t)(core_cc_list.log_buf.index + 4);
}
static void fuzz_irq_exit(int irq) {
    core_cc_list.log_buf.buffer[(uint16_t)core_cc_list.log_buf.index / 4] = 0xFFFFFFFD;
    core_cc_list.log_buf.index = (uint16_t)(core_cc_list.log_buf.index + 4);
}

int fuzz_libAFL_init(char *numbers);

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

static void fuzz_finish() {
    // Wait until tracer catches up with the logged pc's, otherwise coverage is innacurate
    uint16_t idx = (uint16_t)(core_cc_list.log_buf.index - 4);
    uint32_t last_log = core_cc_list.log_buf.buffer[idx / 4];

    while (g_prev_pc != last_log);

    g_prev_pc = 0;

    // if fuzzer not started, return
    if (fuzz_started == false) return;

    if (forced_trace) fuzz_trace_finish_run();

    // if fuzzer had previously consumed input, mark empty and ready for next
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

    // Doesn't include stack information
    if (array[count-1] != 0) {
        printf("No stack information\n");
        return 0;
    }

    fclose(f);

    *out_count = count;   // number of uint32_t entries (not pairs!)
    return array;
}

// Initialization related things that should wait until anchor is reached
static void fuzz_initialize_anchor(char* args) {
    if (!args) {
        utils_die("Error, anchor doesn't have arguments");
    }

    // clear coverage from the run up to the anchor
    memset(CVG, 0, sizeof(CVG));

    atomic_init(&fuzz_buffer.state, FUZZ_EMPTY);
    atomic_init(&fuzz_buffer.assert, 0);
    fuzz_buffer.buffer = NULL;

    if (!fuzz_libAFL_init(args)) {
        utils_die("[anchor] Failed initialization");
    }

    // start tracing our coverage now that we've reached anchor
    if (forced_trace && (fuzz_trace_init() != 0)) {
        utils_die("[anchor] Failed to initialize tracing");
    }
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
    if (!fuzz_started) {
        fuzz_initialize_anchor(udata);
        // save regs
        for (int i = 0; i < 16; i++) {
            saved_regs[i] = qemu_get_register(i);
        }

        // first, rewrite the stack part of the wlist with the actual stack base to top
        wlist[wlist_count - 1] = wlist[wlist_count - 2] - saved_regs[15]; // size
        wlist[wlist_count - 2] = saved_regs[15]; // stack pointer

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
    } else {
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

            if (forced_trace) fuzz_trace_add_value(val);
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
    fuzz_dump_bbl();
    fuzz_serialize_coverage(CVG_PATH);
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

    fuzz_register_callback(fuzz_plugin_lwip_http_fuzzer);

    wlist = fuzz_get_writable_ranges(WLIST_PATH, &wlist_count);
    if (wlist == NULL || (wlist_count & 1)) {
        utils_die("[anchor] Couldn't parse writable memory definitions");
    }

    virtual_register("assert", virt_assert);
    virtual_register("anchor", anchor);

    return 0;
}
