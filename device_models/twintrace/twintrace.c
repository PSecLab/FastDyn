// device_models/twintrace.c

#include <device.h>
#include <hw.h>
#include <utils.h>

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// Your ConfigSection contains:
//   twintrace_mode_t twintrace_mode;
//   const char* twintrace_bin;

enum { TT_R = 0, TT_W = 1 };

// -----------------------
// Packed format structs
// -----------------------
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];       // "TTTR"
    uint32_t version;        // 1
    uint32_t record_size;    // 40
    uint64_t count;          // number of records
} tt_hdr_t;

typedef struct __attribute__((packed)) {
    uint64_t icount;
    uint64_t pc;
    uint64_t addr;
    uint64_t value;
    uint32_t size;
    uint32_t type;           // 0=READ, 1=WRITE
} tt_rec_t;

_Static_assert(sizeof(tt_hdr_t) == 20, "tt_hdr_t must be 20 bytes");
_Static_assert(sizeof(tt_rec_t) == 40, "tt_rec_t must be 40 bytes");

// -----------------------
// Record mode: hardware backend (passthrough semantics)
// -----------------------
static hw_t *hw = NULL;
static pthread_t dev_thread;
static pthread_mutex_t hw_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t irq_cv = PTHREAD_COND_INITIALIZER;
static int irq_pending;

// -----------------------
// Replay mode: mapped tape
// -----------------------
typedef struct {
    int fd;
    size_t map_len;
    uint8_t *map;

    tt_hdr_t hdr;          // copied
    const uint8_t *rec_bytes;  // points to record array (bytes)
    uint64_t idx;

    bool strict_addr;
    bool strict_size;
    bool strict_pc;
    bool strict_value_on_write;
} tt_replay_t;

static tt_replay_t g_rep;

static twintrace_mode_t g_mode = TT_OFF;
static const char *g_bin_path = NULL;

// -----------------------
// Helpers
// -----------------------
static void die_sys(const char *msg, const char *path) {
    fprintf(stderr, "[twintrace] %s (%s): %s\n",
            msg, path ? path : "(null)", strerror(errno));
    utils_die("twintrace fatal");
}

static void tt_abort(const char *why,
                     hwaddr addr, unsigned size, uint64_t pc, uint64_t val,
                     const tt_rec_t *exp)
{
    fprintf(stderr,
        "\n[twintrace] %s\n"
        "  idx=%" PRIu64 " / %" PRIu64 "\n"
        "  got: addr=0x%08" PRIx64 " size=%u pc=0x%08" PRIx64 " val=0x%08" PRIx64 "\n"
        "  exp: %s icount=%" PRIu64 " addr=0x%08" PRIx64 " size=%u pc=0x%08" PRIx64 " val=0x%08" PRIx64 "\n\n",
        why,
        g_rep.idx, g_rep.hdr.count,
        (uint64_t)addr, size, pc, val,
        (exp && exp->type == TT_R) ? "READ " : "WRITE",
        exp ? exp->icount : 0,
        exp ? exp->addr : 0,
        exp ? exp->size : 0,
        exp ? exp->pc : 0,
        exp ? exp->value : 0
    );
    utils_die("twintrace divergence");
}

static inline void tt_get_rec(uint64_t idx, tt_rec_t *out)
{
    // Avoid unaligned packed reads by memcpy
    const uint8_t *p = g_rep.rec_bytes + idx * sizeof(tt_rec_t);
    memcpy(out, p, sizeof(tt_rec_t));
}

static void tt_replay_open(const char *path)
{
    memset(&g_rep, 0, sizeof(g_rep));
    g_rep.fd = -1;

    g_rep.fd = open(path, O_RDONLY);
    if (g_rep.fd < 0) die_sys("open replay file failed", path);

    struct stat st;
    if (fstat(g_rep.fd, &st) != 0) die_sys("fstat replay file failed", path);
    if (st.st_size < (off_t)sizeof(tt_hdr_t)) utils_die("twintrace: replay file too small");

    g_rep.map_len = (size_t)st.st_size;
    g_rep.map = (uint8_t *)mmap(NULL, g_rep.map_len, PROT_READ, MAP_PRIVATE, g_rep.fd, 0);
    if (g_rep.map == MAP_FAILED) die_sys("mmap replay file failed", path);

    // Copy header out (safe even if packed)
    memcpy(&g_rep.hdr, g_rep.map, sizeof(tt_hdr_t));

    if (memcmp(g_rep.hdr.magic, "TTTR", 4) != 0) utils_die("twintrace: bad magic");
    if (g_rep.hdr.version != 1) utils_die("twintrace: unsupported version");
    if (g_rep.hdr.record_size != sizeof(tt_rec_t)) utils_die("twintrace: record_size mismatch");

    size_t need = sizeof(tt_hdr_t) + (size_t)g_rep.hdr.count * sizeof(tt_rec_t);
    if (need > g_rep.map_len) utils_die("twintrace: truncated replay file");

    g_rep.rec_bytes = g_rep.map + sizeof(tt_hdr_t);
    g_rep.idx = 0;

    // Strictness defaults (tune as you like)
    g_rep.strict_addr = true;
    g_rep.strict_size = true;
    g_rep.strict_pc = false;
    g_rep.strict_value_on_write = true;

    fprintf(stderr, "[twintrace] replay loaded: %s (count=%" PRIu64 ")\n",
            path, g_rep.hdr.count);
}

// -----------------------
// IRQ thread (record mode only, same idea as passthrough)
// -----------------------
static void* dev_thread_fn(void* arg)
{
    (void)arg;
    while (1) {
        pthread_mutex_lock(&hw_mutex);
        while (irq_pending) {
            pthread_cond_wait(&irq_cv, &hw_mutex);
        }

        if (hw_board_halted(hw)) {
            int firing_line = (int)hw_read_reg(hw, 0);
            irq_pending = firing_line;
            pthread_mutex_unlock(&hw_mutex);
            qemu_plugin_raise_irq(firing_line, false);
        } else {
            pthread_mutex_unlock(&hw_mutex);
            sleep(5);
        }
    }
    return NULL;
}

static int twintrace_serve(int line)
{
    if (g_mode != TT_RECORD) return 0;

    pthread_mutex_lock(&hw_mutex);
    hw_write_reg(hw, 1, line);
    hw_board_run(hw);
    irq_pending = 0;
    pthread_cond_signal(&irq_cv);
    pthread_mutex_unlock(&hw_mutex);
    return 0;
}

static int twintrace_interrupt(int line)
{
    (void)line;
    return 0;
}

// -----------------------
// MMIO ops
// -----------------------
static uint64_t twintrace_read(void *opaque, hwaddr address, unsigned size, uint64_t pc)
{
    (void)opaque;

    if (g_mode == TT_RECORD) {
        uint32_t value_read = 0;

        pthread_mutex_lock(&hw_mutex);
        if (!hw) { pthread_mutex_unlock(&hw_mutex); utils_die("HW handle not initialized"); }
        int status = hw_read32(hw, address, &value_read);
        pthread_mutex_unlock(&hw_mutex);

        if (status != 0) utils_die("HW Read Failed");
        return value_read;
    }

    // replay
    if (g_rep.idx >= g_rep.hdr.count) utils_die("twintrace: ran out of records");

    tt_rec_t e;
    tt_get_rec(g_rep.idx, &e);

    if (e.type != TT_R) tt_abort("expected READ, got WRITE", address, size, pc, 0, &e);
    if (g_rep.strict_addr && e.addr != (uint64_t)address) tt_abort("READ addr mismatch", address, size, pc, 0, &e);
    if (g_rep.strict_size && e.size != (uint32_t)size) tt_abort("READ size mismatch", address, size, pc, 0, &e);
    if (g_rep.strict_pc && e.pc != pc) tt_abort("READ pc mismatch", address, size, pc, 0, &e);

    g_rep.idx++;
    return e.value;
}

static void twintrace_write(void *opaque, hwaddr address, uint64_t value, unsigned size, uint64_t pc)
{
    (void)opaque;

    if (g_mode == TT_RECORD) {
        pthread_mutex_lock(&hw_mutex);
        if (!hw) { pthread_mutex_unlock(&hw_mutex); utils_die("HW handle not initialized"); }

        int status;
        if (size == 1) status = hw_write8(hw, address, (uint32_t)value);
        else           status = hw_write32(hw, address, (uint32_t)value);

        pthread_mutex_unlock(&hw_mutex);

        if (status != 0) utils_die("HW Write Failed");
        return;
    }

    // replay
    if (g_rep.idx >= g_rep.hdr.count) utils_die("twintrace: ran out of records");

    tt_rec_t e;
    tt_get_rec(g_rep.idx, &e);

    if (e.type != TT_W) tt_abort("expected WRITE, got READ", address, size, pc, value, &e);
    if (g_rep.strict_addr && e.addr != (uint64_t)address) tt_abort("WRITE addr mismatch", address, size, pc, value, &e);
    if (g_rep.strict_size && e.size != (uint32_t)size) tt_abort("WRITE size mismatch", address, size, pc, value, &e);
    if (g_rep.strict_pc && e.pc != pc) tt_abort("WRITE pc mismatch", address, size, pc, value, &e);
    if (g_rep.strict_value_on_write && e.value != value) tt_abort("WRITE value mismatch", address, size, pc, value, &e);

    g_rep.idx++;
}

// -----------------------
// init
// -----------------------
static int twintrace_init(ConfigSection* model_info);

DeviceModel twintrace_model_def = {
    .name      = "twintrace",
    .read      = twintrace_read,
    .write     = twintrace_write,
    .init      = twintrace_init,
    .serve     = twintrace_serve,
    .interrupt = twintrace_interrupt,
};

static int twintrace_init(ConfigSection* model_info)
{
    // Register address ranges
    Range ranges[10];
    utils_parse_ranges(model_info->overall_range_count, model_info->overall_ranges, ranges);
    for (int i = 0; i < model_info->overall_range_count; i++) {
        dev_register_device_model(ranges[i].start, ranges[i].end, &twintrace_model_def);
    }

    g_mode = model_info->twintrace_mode;
    g_bin_path = model_info->twintrace_bin;

    fprintf(stderr, "[twintrace] mode=%d bin=%s backend=%s\n",
            (int)g_mode,
            g_bin_path ? g_bin_path : "(null)",
            model_info->backend ? model_info->backend : "(null)");

    if (g_mode == TT_RECORD) {
        // behave like passthrough
        hw = hw_connect(model_info->backend, NULL, 0);
        if (!hw) utils_die("HW connection failed");

        if (pthread_create(&dev_thread, NULL, dev_thread_fn, NULL) != 0) {
            perror("Failed to create IRQ thread");
            return 1;
        }
    } else if (g_mode == TT_REPLAY) {
        if (!g_bin_path || !g_bin_path[0]) utils_die("twintrace_bin missing for replay");
        tt_replay_open(g_bin_path);
    } else {
        utils_die("twintrace: TT_OFF but model selected");
    }

    return 0;
}
