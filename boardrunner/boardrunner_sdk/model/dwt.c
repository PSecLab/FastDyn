#include <stdint.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define DWT_BASE                0xE0001000ULL
#define DWT_CTRL_OFF            0x000
#define DWT_CYCCNT_OFF          0x004

#define DWT_CTRL_CYCCNTENA      (1u << 0)

/*
 * The firmware is polling CYCCNT in a tight MMIO loop during early boot.
 * QEMU virtual time may not advance between those back-to-back accesses, so
 * we also inject a small synthetic step on each CYCCNT read while enabled to
 * guarantee forward progress.
 */
#define DWT_NOMINAL_HZ          600000000ULL
#define DWT_SYNTHETIC_READ_STEP 1024u

typedef struct {
    uint32_t ctrl;
    uint32_t cyccnt;
    int64_t last_vtime_ns;
} DWTState;

static DWTState g_dwt;

static uint32_t dwt_extract_access(uint32_t reg, uint64_t addr, unsigned size) {
    if (size >= 4) {
        return reg;
    }

    unsigned shift = (unsigned)(addr & 0x3ULL) * 8u;
    uint32_t mask = (1u << (size * 8u)) - 1u;
    return (reg >> shift) & mask;
}

static uint32_t dwt_merge_access(uint32_t old_reg, uint64_t addr, uint64_t value, unsigned size) {
    if (size >= 4) {
        return (uint32_t)value;
    }

    unsigned shift = (unsigned)(addr & 0x3ULL) * 8u;
    uint32_t mask = ((1u << (size * 8u)) - 1u) << shift;
    return (old_reg & ~mask) | ((((uint32_t)value) << shift) & mask);
}

static void dwt_sync_cyccnt(DWTState *s, int force_progress) {
    int64_t now = qemu_plugin_get_virtual_timer();

    if ((s->ctrl & DWT_CTRL_CYCCNTENA) == 0) {
        s->last_vtime_ns = now;
        /* Some Cortex-M QEMU builds consume the CTRL write internally while
         * still forwarding CYCCNT reads to the plugin. Preserve progress for
         * firmware polling loops in that split-routing case. */
        if (force_progress) {
            s->cyccnt += DWT_SYNTHETIC_READ_STEP;
        }
        return;
    }

    uint32_t before = s->cyccnt;

    if (s->last_vtime_ns >= 0 && now > s->last_vtime_ns) {
        uint64_t delta_ns = (uint64_t)(now - s->last_vtime_ns);
        uint64_t add = (delta_ns * DWT_NOMINAL_HZ) / 1000000000ULL;
        s->cyccnt += (uint32_t)add;
    }

    s->last_vtime_ns = now;

    if (force_progress && s->cyccnt == before) {
        s->cyccnt += DWT_SYNTHETIC_READ_STEP;
    }
}

void* dwt_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_dwt, 0, sizeof(g_dwt));
    g_dwt.last_vtime_ns = -1;
    return &g_dwt;
}

uint64_t dwt_read(void *opaque, uint64_t addr, unsigned size) {
    DWTState *s = (DWTState *)opaque;
    uint64_t offset = addr - DWT_BASE;
    uint32_t reg = 0;

    switch (offset & ~0x3ULL) {
        case DWT_CTRL_OFF:
            reg = s->ctrl;
            break;

        case DWT_CYCCNT_OFF:
            dwt_sync_cyccnt(s, 1);
            reg = s->cyccnt;
            break;

        default:
            reg = 0;
            break;
    }

    return (uint64_t)dwt_extract_access(reg, addr, size);
}

void dwt_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    DWTState *s = (DWTState *)opaque;
    uint64_t offset = addr - DWT_BASE;

    switch (offset & ~0x3ULL) {
        case DWT_CTRL_OFF: {
            uint32_t merged = dwt_merge_access(s->ctrl, addr, value, size);
            s->ctrl = merged & DWT_CTRL_CYCCNTENA;
            s->last_vtime_ns = qemu_plugin_get_virtual_timer();
            break;
        }

        case DWT_CYCCNT_OFF: {
            dwt_sync_cyccnt(s, 0);
            s->cyccnt = dwt_merge_access(s->cyccnt, addr, value, size);
            s->last_vtime_ns = qemu_plugin_get_virtual_timer();
            break;
        }

        default:
            break;
    }
}
