#include <stdint.h>
#include <string.h>
#include <device.h>
#include <core.h>

#define DWT_BASE                0xE0001000ULL
#define DWT_CTRL_OFF            0x000
#define DWT_CYCCNT_OFF          0x004

#define DWT_CTRL_CYCCNTENA      (1u << 0)

/*
 * The trace shows firmware spinning in chSysPolledDelayX() on CYCCNT reads.
 * qemu_plugin_get_virtual_timer() may not advance enough during tight MMIO polling,
 * so the counter must also make synchronous forward progress on every read.
 *
 * 500000 matches the observed step size in the trace and exits busy-wait delays
 * quickly while remaining monotonic and stateful.
 */
#define DWT_MIN_READ_ADVANCE    500000u
#define DWT_CYCCNT_HZ           168000000ULL

typedef struct {
    uint32_t ctrl;
    uint32_t cyccnt;
    int64_t last_vtime_ns;
} DWTState;

static DWTState g_dwt;

static uint64_t dwt_size_mask(unsigned size) {
    switch (size) {
    case 1:
        return 0xffu;
    case 2:
        return 0xffffu;
    case 4:
    default:
        return 0xffffffffu;
    }
}

static void dwt_sync_cyccnt(DWTState *s) {
    int64_t now = qemu_plugin_get_virtual_timer();

    if ((s->ctrl & DWT_CTRL_CYCCNTENA) == 0) {
        s->last_vtime_ns = now;
        return;
    }

    uint64_t advance = 0;
    if (now > s->last_vtime_ns) {
        uint64_t delta_ns = (uint64_t)(now - s->last_vtime_ns);
        advance = (delta_ns * DWT_CYCCNT_HZ) / 1000000000ULL;
    }

    if (advance < DWT_MIN_READ_ADVANCE) {
        advance = DWT_MIN_READ_ADVANCE;
    }

    s->cyccnt += (uint32_t)advance;
    s->last_vtime_ns = now;
}

void* dwt_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_dwt, 0, sizeof(g_dwt));

    /*
     * Trace evidence:
     *   READ 0xE0001000 -> 0x1
     * before firmware writes 0x1 back.
     * So expose CYCCNTENA as already set at reset for this target.
     */
    g_dwt.ctrl = DWT_CTRL_CYCCNTENA;
    g_dwt.last_vtime_ns = qemu_plugin_get_virtual_timer();

    return &g_dwt;
}

uint64_t dwt_read(void *opaque, uint64_t addr, unsigned size) {
    DWTState *s = (DWTState *)opaque;
    uint64_t offset = addr - DWT_BASE;
    uint64_t value = 0;

    switch (offset) {
    case DWT_CTRL_OFF:
        value = s->ctrl;
        break;

    case DWT_CYCCNT_OFF:
        dwt_sync_cyccnt(s);
        value = s->cyccnt;
        break;

    default:
        value = 0;
        break;
    }

    return value & dwt_size_mask(size);
}

void dwt_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    DWTState *s = (DWTState *)opaque;
    uint64_t offset = addr - DWT_BASE;
    uint64_t mask = dwt_size_mask(size);

    value &= mask;

    switch (offset) {
    case DWT_CTRL_OFF:
        /*
         * Preserve elapsed count up to the write, then apply the new enable bit.
         * Only CYCCNTENA is modeled because it is the only bit evidenced by trace.
         */
        dwt_sync_cyccnt(s);
        s->ctrl = (s->ctrl & ~DWT_CTRL_CYCCNTENA) |
                  ((uint32_t)value & DWT_CTRL_CYCCNTENA);
        s->last_vtime_ns = qemu_plugin_get_virtual_timer();
        break;

    case DWT_CYCCNT_OFF:
        /*
         * DWT_CYCCNT is writable on Cortex-M; support direct seed/reset.
         */
        s->cyccnt = (uint32_t)value;
        s->last_vtime_ns = qemu_plugin_get_virtual_timer();
        break;

    default:
        break;
    }
}