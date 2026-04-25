// Device Model for DMA_with_DMAMUX1

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define DMA1_BASE           0x40020000ULL
#define DMAMUX1_BASE        0x40020800ULL

#define DMA1_MMIO_SIZE      0x400
#define DMAMUX1_MMIO_SIZE   0x100

#define DMA1_IRQ_LINE       28  /* NVIC IRQn 12 + 16, from trace */

#define DMA_FIRST_EVENT_DELAY_NS 1ULL
#define DMA_PERIOD_NS            13367000ULL  /* trace-derived average interval */

/* DMA global registers */
#define DMA_LISR_OFF        0x00
#define DMA_HISR_OFF        0x04
#define DMA_LIFCR_OFF       0x08
#define DMA_HIFCR_OFF       0x0C

/* Stream register layout */
#define DMA_STREAM_BASE(n)  (0x10u + (0x18u * (n)))
#define DMA_SxCR_OFF(n)     (DMA_STREAM_BASE(n) + 0x00u)
#define DMA_SxNDTR_OFF(n)   (DMA_STREAM_BASE(n) + 0x04u)
#define DMA_SxPAR_OFF(n)    (DMA_STREAM_BASE(n) + 0x08u)
#define DMA_SxM0AR_OFF(n)   (DMA_STREAM_BASE(n) + 0x0Cu)
#define DMA_SxM1AR_OFF(n)   (DMA_STREAM_BASE(n) + 0x10u)
#define DMA_SxFCR_OFF(n)    (DMA_STREAM_BASE(n) + 0x14u)

/* DMA Stream CR bits */
#define DMA_SxCR_EN         (1u << 0)
#define DMA_SxCR_DMEIE      (1u << 1)
#define DMA_SxCR_TEIE       (1u << 2)
#define DMA_SxCR_HTIE       (1u << 3)
#define DMA_SxCR_TCIE       (1u << 4)
#define DMA_SxCR_CIRC       (1u << 8)
#define DMA_SxCR_MINC       (1u << 10)
#define DMA_SxCR_PSIZE_SHIFT 11
#define DMA_SxCR_MSIZE_SHIFT 13
#define DMA_SxCR_DBM        (1u << 18)
#define DMA_SxCR_CT         (1u << 19)

/* DMA Stream FCR */
#define DMA_SxFCR_RESET     0x21u
#define DMA_SxFCR_RW_MASK   0x87u   /* FEIE, DMDIS, FTH */
#define DMA_SxFCR_RO_FS     0x20u   /* keep FIFO status-looking reset bit */

/* LISR bits for Stream 1 */
#define DMA_LISR_FEIF1      (1u << 6)
#define DMA_LISR_DMEIF1     (1u << 8)
#define DMA_LISR_TEIF1      (1u << 9)
#define DMA_LISR_HTIF1      (1u << 10)
#define DMA_LISR_TCIF1      (1u << 11)

/* DMAMUX */
#define DMAMUX_CxCR_MAX_CH  16
#define DMAMUX_CxCR_END     0x40
#define DMAMUX_CSR_OFF      0x80
#define DMAMUX_CFR_OFF      0x84

typedef struct DMAStreamState {
    uint32_t cr;
    uint32_t ndtr;
    uint32_t par;
    uint32_t m0ar;
    uint32_t m1ar;
    uint32_t fcr;
    uint32_t initial_ndtr;
} DMAStreamState;

typedef struct DMAWithDMAMUX1State {
    uint32_t lisr;
    uint32_t hisr;

    DMAStreamState stream[8];

    uint32_t dmamux_cxcr[DMAMUX_CxCR_MAX_CH];
    uint32_t dmamux_csr;

    uint64_t timer;
    bool event_scheduled;
    bool stream1_started_once;
    uint64_t next_event_ns;

    uint32_t synthetic_sample;
} DMAWithDMAMUX1State;

static DMAWithDMAMUX1State g_dma_with_dmamux1;

static uint64_t reg_extract_u32(uint32_t reg, unsigned size, hwaddr addr)
{
    unsigned shift = (unsigned)(addr & 0x3u) * 8u;

    switch (size) {
    case 1:
        return (reg >> shift) & 0xffu;
    case 2:
        return (reg >> shift) & 0xffffu;
    default:
        return reg;
    }
}

static uint32_t reg_merge_u32(uint32_t old, uint64_t value, unsigned size, hwaddr addr)
{
    unsigned shift = (unsigned)(addr & 0x3u) * 8u;
    uint32_t mask;

    switch (size) {
    case 1:
        mask = 0xffu << shift;
        break;
    case 2:
        mask = 0xffffu << shift;
        break;
    default:
        mask = 0xffffffffu;
        shift = 0;
        break;
    }

    return (old & ~mask) | ((((uint32_t)value) << shift) & mask);
}

static unsigned dma_mem_size_bytes(uint32_t cr)
{
    unsigned msize = (cr >> DMA_SxCR_MSIZE_SHIFT) & 0x3u;

    switch (msize) {
    case 0:
        return 1;
    case 1:
        return 2;
    default:
        return 4;
    }
}

static bool dma_stream1_request_enabled(DMAWithDMAMUX1State *s)
{
    /* DMAMUX1 channel 1 corresponds to traced C1CR at offset 0x04. */
    return (s->dmamux_cxcr[1] & 0x7fu) != 0;
}

static bool dma_stream1_enabled(DMAWithDMAMUX1State *s)
{
    return (s->stream[1].cr & DMA_SxCR_EN) != 0;
}

static bool dma_stream1_active(DMAWithDMAMUX1State *s)
{
    return dma_stream1_enabled(s) && dma_stream1_request_enabled(s);
}

static void dma_raise_irq_if_needed(DMAWithDMAMUX1State *s)
{
    uint32_t cr = s->stream[1].cr;
    bool pending = false;

    if ((s->lisr & DMA_LISR_HTIF1) && (cr & DMA_SxCR_HTIE)) {
        pending = true;
    }
    if ((s->lisr & DMA_LISR_TCIF1) && (cr & DMA_SxCR_TCIE)) {
        pending = true;
    }
    if ((s->lisr & DMA_LISR_TEIF1) && (cr & DMA_SxCR_TEIE)) {
        pending = true;
    }

    if (pending) {
        qemu_plugin_raise_irq(DMA1_IRQ_LINE, false);
    }
}

static void dma_write_synthetic_buffer(DMAWithDMAMUX1State *s, DMAStreamState *st)
{
    uint32_t addr;
    uint32_t count;
    unsigned elem_size;
    uint8_t buf[64];

    if ((st->cr & DMA_SxCR_DBM) && (st->cr & DMA_SxCR_CT)) {
        addr = st->m1ar;
    } else {
        addr = st->m0ar;
    }

    count = st->initial_ndtr ? st->initial_ndtr : st->ndtr;
    if (addr == 0 || count == 0) {
        return;
    }

    elem_size = dma_mem_size_bytes(st->cr);

    while (count) {
        uint32_t remaining = count;
        uint32_t max_elems = (uint32_t)(sizeof(buf) / elem_size);
        uint32_t elems = (remaining < max_elems) ? remaining : max_elems;
        uint32_t i;

        memset(buf, 0, sizeof(buf));

        for (i = 0; i < elems; i++) {
            uint32_t sample = s->synthetic_sample++;
            uint32_t off = i * elem_size;

            if (elem_size == 1) {
                buf[off] = (uint8_t)(sample & 0xffu);
            } else if (elem_size == 2) {
                buf[off + 0] = (uint8_t)(sample & 0xffu);
                buf[off + 1] = (uint8_t)((sample >> 8) & 0xffu);
            } else {
                buf[off + 0] = (uint8_t)(sample & 0xffu);
                buf[off + 1] = (uint8_t)((sample >> 8) & 0xffu);
                buf[off + 2] = (uint8_t)((sample >> 16) & 0xffu);
                buf[off + 3] = (uint8_t)((sample >> 24) & 0xffu);
            }
        }

        qemu_plugin_write_memory(addr, buf, (int)(elems * elem_size));

        if (st->cr & DMA_SxCR_MINC) {
            addr += elems * elem_size;
        }

        count -= elems;
    }
}

static void dma_complete_stream1_transfer(DMAWithDMAMUX1State *s)
{
    DMAStreamState *st = &s->stream[1];

    if (!dma_stream1_active(s)) {
        return;
    }

    /*
     * We cannot read the ADC MMIO data register through qemu_plugin_read_memory
     * because MMIO re-entrant accesses are blocked, so synthesize RAM data.
     */
    dma_write_synthetic_buffer(s, st);

    /* Trace shows both HTIF1 and TCIF1 set together as 0xC00. */
    s->lisr |= DMA_LISR_HTIF1 | DMA_LISR_TCIF1;
    s->stream1_started_once = true;

    if (st->cr & DMA_SxCR_DBM) {
        st->cr ^= DMA_SxCR_CT;
    }

    if (st->cr & DMA_SxCR_CIRC) {
        st->ndtr = st->initial_ndtr;
    } else {
        st->ndtr = 0;
        st->cr &= ~DMA_SxCR_EN;
        s->event_scheduled = false;
    }

    dma_raise_irq_if_needed(s);
}

static void dma_schedule_next_event(DMAWithDMAMUX1State *s)
{
    int64_t now = qemu_plugin_get_virtual_timer();
    uint64_t delay;

    if (now < 0) {
        now = 0;
    }

    if (!dma_stream1_active(s)) {
        s->event_scheduled = false;
        return;
    }

    /*
     * The first DMA event after stream activation must happen quickly enough
     * for firmware to observe the expected stream1 IRQ path. Subsequent events
     * in circular mode can use the trace-derived longer cadence.
     */
    delay = s->stream1_started_once ? DMA_PERIOD_NS : DMA_FIRST_EVENT_DELAY_NS;

    s->next_event_ns = (uint64_t)now + delay;
    s->event_scheduled = true;
    qemu_plugin_timer_alarm(s->timer, s->next_event_ns);
}

static void dma_maybe_fire_due_event(DMAWithDMAMUX1State *s)
{
    int64_t now;

    if (!s->event_scheduled) {
        return;
    }

    if (!dma_stream1_active(s)) {
        s->event_scheduled = false;
        return;
    }

    now = qemu_plugin_get_virtual_timer();
    if (now < 0) {
        now = 0;
    }

    if ((uint64_t)now < s->next_event_ns) {
        return;
    }

    dma_complete_stream1_transfer(s);

    if (dma_stream1_active(s)) {
        s->next_event_ns = (uint64_t)now + DMA_PERIOD_NS;
        qemu_plugin_timer_alarm(s->timer, s->next_event_ns);
    }
}

static void dma_timer_cb(void *opaque)
{
    DMAWithDMAMUX1State *s = (DMAWithDMAMUX1State *)opaque;

    dma_maybe_fire_due_event(s);
}

static uint64_t dma_read_stream_reg(DMAWithDMAMUX1State *s, hwaddr offset, unsigned size)
{
    unsigned i;

    for (i = 0; i < 8; i++) {
        if (offset == DMA_SxCR_OFF(i)) {
            return reg_extract_u32(s->stream[i].cr, size, offset);
        }
        if (offset == DMA_SxNDTR_OFF(i)) {
            return reg_extract_u32(s->stream[i].ndtr, size, offset);
        }
        if (offset == DMA_SxPAR_OFF(i)) {
            return reg_extract_u32(s->stream[i].par, size, offset);
        }
        if (offset == DMA_SxM0AR_OFF(i)) {
            return reg_extract_u32(s->stream[i].m0ar, size, offset);
        }
        if (offset == DMA_SxM1AR_OFF(i)) {
            return reg_extract_u32(s->stream[i].m1ar, size, offset);
        }
        if (offset == DMA_SxFCR_OFF(i)) {
            return reg_extract_u32(s->stream[i].fcr, size, offset);
        }
    }

    return 0;
}

static bool dma_write_stream_reg(DMAWithDMAMUX1State *s, hwaddr offset, uint64_t value, unsigned size)
{
    unsigned i;

    for (i = 0; i < 8; i++) {
        if (offset == DMA_SxCR_OFF(i)) {
            bool old_active = false;
            bool new_active;

            if (i == 1) {
                old_active = dma_stream1_active(s);
            }

            s->stream[i].cr = reg_merge_u32(s->stream[i].cr, value, size, offset);

            if (i == 1) {
                new_active = dma_stream1_active(s);

                if (!old_active && new_active) {
                    s->stream1_started_once = false;
                }

                if (new_active) {
                    dma_schedule_next_event(s);
                } else {
                    s->event_scheduled = false;
                }
            }
            return true;
        }

        if (offset == DMA_SxNDTR_OFF(i)) {
            s->stream[i].ndtr = reg_merge_u32(s->stream[i].ndtr, value, size, offset);
            s->stream[i].initial_ndtr = s->stream[i].ndtr;
            return true;
        }

        if (offset == DMA_SxPAR_OFF(i)) {
            s->stream[i].par = reg_merge_u32(s->stream[i].par, value, size, offset);
            return true;
        }

        if (offset == DMA_SxM0AR_OFF(i)) {
            s->stream[i].m0ar = reg_merge_u32(s->stream[i].m0ar, value, size, offset);
            return true;
        }

        if (offset == DMA_SxM1AR_OFF(i)) {
            s->stream[i].m1ar = reg_merge_u32(s->stream[i].m1ar, value, size, offset);
            return true;
        }

        if (offset == DMA_SxFCR_OFF(i)) {
            uint32_t merged = reg_merge_u32(s->stream[i].fcr, value, size, offset);
            s->stream[i].fcr = DMA_SxFCR_RO_FS | (merged & DMA_SxFCR_RW_MASK);
            return true;
        }
    }

    return false;
}

uint64_t dma_with_dmamux1_read(void *opaque, hwaddr addr, unsigned size)
{
    DMAWithDMAMUX1State *s = (DMAWithDMAMUX1State *)opaque;

    if (s == NULL) {
        s = &g_dma_with_dmamux1;
    }

    dma_maybe_fire_due_event(s);

    if (addr >= DMA1_BASE && addr < (DMA1_BASE + DMA1_MMIO_SIZE)) {
        hwaddr offset = addr - DMA1_BASE;

        /*
         * Timer callbacks only fire at TB boundaries. If firmware polls DMA
         * state in a tight loop immediately after enabling the stream, virtual
         * time may not advance enough for the scheduled first event to arrive.
         * Complete the first stream1 transfer synchronously on the first poll
         * of the relevant DMA status/control registers.
         */
        if (s->event_scheduled &&
            !s->stream1_started_once &&
            s->lisr == 0 &&
            dma_stream1_active(s) &&
            (offset == DMA_LISR_OFF || offset == DMA_SxCR_OFF(1))) {
            dma_complete_stream1_transfer(s);
        }

        switch (offset) {
        case DMA_LISR_OFF:
            return reg_extract_u32(s->lisr, size, offset);
        case DMA_HISR_OFF:
            return reg_extract_u32(s->hisr, size, offset);
        default:
            return dma_read_stream_reg(s, offset, size);
        }
    }

    if (addr >= DMAMUX1_BASE && addr < (DMAMUX1_BASE + DMAMUX1_MMIO_SIZE)) {
        hwaddr offset = addr - DMAMUX1_BASE;

        if (offset < DMAMUX_CxCR_END && (offset & 0x3u) == 0) {
            unsigned ch = (unsigned)(offset >> 2);
            if (ch < DMAMUX_CxCR_MAX_CH) {
                return reg_extract_u32(s->dmamux_cxcr[ch], size, offset);
            }
        }

        if (offset == DMAMUX_CSR_OFF) {
            return reg_extract_u32(s->dmamux_csr, size, offset);
        }

        /* CFR is write-only in practice; return 0 on reads. */
        if (offset == DMAMUX_CFR_OFF) {
            return 0;
        }
    }

    return 0;
}

void dma_with_dmamux1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    DMAWithDMAMUX1State *s = (DMAWithDMAMUX1State *)opaque;

    if (s == NULL) {
        s = &g_dma_with_dmamux1;
    }

    dma_maybe_fire_due_event(s);

    if (addr >= DMA1_BASE && addr < (DMA1_BASE + DMA1_MMIO_SIZE)) {
        hwaddr offset = addr - DMA1_BASE;

        switch (offset) {
        case DMA_LIFCR_OFF: {
            uint32_t merged = reg_merge_u32(0, value, size, offset);
            s->lisr &= ~merged;
            return;
        }
        case DMA_HIFCR_OFF: {
            uint32_t merged = reg_merge_u32(0, value, size, offset);
            s->hisr &= ~merged;
            return;
        }
        default:
            if (dma_write_stream_reg(s, offset, value, size)) {
                return;
            }
            return;
        }
    }

    if (addr >= DMAMUX1_BASE && addr < (DMAMUX1_BASE + DMAMUX1_MMIO_SIZE)) {
        hwaddr offset = addr - DMAMUX1_BASE;

        if (offset < DMAMUX_CxCR_END && (offset & 0x3u) == 0) {
            unsigned ch = (unsigned)(offset >> 2);
            if (ch < DMAMUX_CxCR_MAX_CH) {
                bool old_active = false;
                bool new_active = false;

                if (ch == 1) {
                    old_active = dma_stream1_active(s);
                }

                s->dmamux_cxcr[ch] = reg_merge_u32(s->dmamux_cxcr[ch], value, size, offset);

                if (ch == 1) {
                    new_active = dma_stream1_active(s);

                    if (!old_active && new_active) {
                        s->stream1_started_once = false;
                    }

                    if (new_active) {
                        dma_schedule_next_event(s);
                    } else {
                        s->event_scheduled = false;
                    }
                }
                return;
            }
        }

        if (offset == DMAMUX_CFR_OFF) {
            uint32_t merged = reg_merge_u32(0, value, size, offset);
            s->dmamux_csr &= ~merged;
            return;
        }

        return;
    }
}

/*
 * The prompt does not provide any ConfigSection accessor API, so this model uses
 * trace-derived base addresses and IRQ routing and keeps model_info unused.
 */
void* dma_with_dmamux1_init(ConfigSection* model_info)
{
    unsigned i;
    (void)model_info;

    memset(&g_dma_with_dmamux1, 0, sizeof(g_dma_with_dmamux1));

    for (i = 0; i < 8; i++) {
        g_dma_with_dmamux1.stream[i].fcr = DMA_SxFCR_RESET;
    }

    g_dma_with_dmamux1.timer =
        qemu_plugin_timer_new_ns(dma_timer_cb, &g_dma_with_dmamux1);

    return &g_dma_with_dmamux1;
}