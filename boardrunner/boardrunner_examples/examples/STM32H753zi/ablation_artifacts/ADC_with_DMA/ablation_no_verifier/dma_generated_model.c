// Device Model for DMA_with_DMAMUX1

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define DMA1_BASE        0x40020000ULL
#define DMAMUX1_BASE     0x40020800ULL

#define DMA1_NUM_STREAMS     8
#define DMAMUX1_NUM_CHANNELS 16
#define API_DMA_IDS          16

#define DMA_LISR_OFF   0x00
#define DMA_HISR_OFF   0x04
#define DMA_LIFCR_OFF  0x08
#define DMA_HIFCR_OFF  0x0C

#define DMA_STREAM_BASE 0x10
#define DMA_STREAM_STRIDE 0x18

#define DMA_SXCR_OFF    0x00
#define DMA_SXNDTR_OFF  0x04
#define DMA_SXPAR_OFF   0x08
#define DMA_SXM0AR_OFF  0x0C
#define DMA_SXM1AR_OFF  0x10
#define DMA_SXFCR_OFF   0x14

#define DMAMUX_CXCR_BASE 0x00
#define DMAMUX_CXCR_STRIDE 0x04
#define DMAMUX_CSR_OFF   0x80
#define DMAMUX_CFR_OFF   0x84

#define BIT(x) (1u << (x))

/* DMA SxCR bits used */
#define SxCR_EN      BIT(0)
#define SxCR_DMEIE   BIT(1)
#define SxCR_TEIE    BIT(2)
#define SxCR_HTIE    BIT(3)
#define SxCR_TCIE    BIT(4)
#define SxCR_DIR_Pos 6
#define SxCR_DIR_Msk (3u << SxCR_DIR_Pos)
#define SxCR_CIRC    BIT(8)
#define SxCR_PINC    BIT(9)
#define SxCR_MINC    BIT(10)
#define SxCR_PSIZE_Pos 11
#define SxCR_PSIZE_Msk (3u << SxCR_PSIZE_Pos)
#define SxCR_MSIZE_Pos 13
#define SxCR_MSIZE_Msk (3u << SxCR_MSIZE_Pos)
#define SxCR_DBM     BIT(18)
#define SxCR_CT      BIT(19)

/* Internal flag bits */
#define DMA_FLAG_FE  BIT(0)
#define DMA_FLAG_DME BIT(1)
#define DMA_FLAG_TE  BIT(2)
#define DMA_FLAG_HT  BIT(3)
#define DMA_FLAG_TC  BIT(4)

typedef struct DMAStreamState {
    uint32_t cr;
    uint32_t ndtr;
    uint32_t par;
    uint32_t m0ar;
    uint32_t m1ar;
    uint32_t fcr;

    /* Internal runtime state */
    uint32_t initial_ndtr;
    uint32_t cur_par;
    uint32_t cur_m0ar;
    uint32_t cur_m1ar;
    uint8_t flags;
} DMAStreamState;

struct DMAWithDMAMUX1State;

typedef struct DMAReqCtx {
    struct DMAWithDMAMUX1State *s;
    int id;
} DMAReqCtx;

typedef struct DMAWithDMAMUX1State {
    DMAStreamState stream[DMA1_NUM_STREAMS];

    uint32_t dmamux_cxcr[DMAMUX1_NUM_CHANNELS];
    uint32_t dmamux_csr;

    DMAReqCtx req_ctx[API_DMA_IDS];
} DMAWithDMAMUX1State;

static DMAWithDMAMUX1State g_dma_with_dmamux1;

/* Only Stream 1 IRQ is evidenced in the trace:
 * NVIC IRQn = 12, so pass 28 to qemu_plugin_raise_irq().
 */
static const int dma1_irq_exc[DMA1_NUM_STREAMS] = {
    -1, 28, -1, -1, -1, -1, -1, -1
};

static uint32_t reg_mask_for_size(unsigned size)
{
    switch (size) {
    case 1: return 0xFFu;
    case 2: return 0xFFFFu;
    default: return 0xFFFFFFFFu;
    }
}

static uint32_t reg_extract(uint32_t reg, hwaddr addr, unsigned size)
{
    unsigned shift = (unsigned)(addr & 0x3u) * 8u;
    uint32_t mask = reg_mask_for_size(size);
    return (reg >> shift) & mask;
}

static uint32_t reg_apply_write(uint32_t reg, hwaddr addr, uint64_t value, unsigned size)
{
    unsigned shift = (unsigned)(addr & 0x3u) * 8u;
    uint32_t mask = reg_mask_for_size(size);
    reg &= ~(mask << shift);
    reg |= ((uint32_t)value & mask) << shift;
    return reg;
}

static unsigned dma_size_bytes(uint32_t size_field)
{
    switch (size_field & 0x3u) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 4;
    default: return 4;
    }
}

static unsigned dma_psize_bytes(DMAStreamState *st)
{
    return dma_size_bytes((st->cr & SxCR_PSIZE_Msk) >> SxCR_PSIZE_Pos);
}

static unsigned dma_msize_bytes(DMAStreamState *st)
{
    return dma_size_bytes((st->cr & SxCR_MSIZE_Msk) >> SxCR_MSIZE_Pos);
}

static int dma_dir(DMAStreamState *st)
{
    return (int)((st->cr & SxCR_DIR_Msk) >> SxCR_DIR_Pos);
}

static uint32_t *dma_current_mem_ptr(DMAStreamState *st)
{
    if ((st->cr & SxCR_DBM) && (st->cr & SxCR_CT)) {
        return &st->cur_m1ar;
    }
    return &st->cur_m0ar;
}

static void dma_reload_runtime(DMAStreamState *st)
{
    st->cur_par = st->par;
    st->cur_m0ar = st->m0ar;
    st->cur_m1ar = st->m1ar;
}

static void dma_start_stream(DMAStreamState *st)
{
    st->initial_ndtr = st->ndtr;
    dma_reload_runtime(st);
}

static void dma_raise_stream_irq(DMAWithDMAMUX1State *s, int stream_idx, uint8_t flag)
{
    (void)s;
    int exc = -1;
    DMAStreamState *st;

    if (stream_idx < 0 || stream_idx >= DMA1_NUM_STREAMS) {
        return;
    }

    exc = dma1_irq_exc[stream_idx];
    if (exc < 0) {
        return;
    }

    st = &g_dma_with_dmamux1.stream[stream_idx];

    if ((flag == DMA_FLAG_HT && (st->cr & SxCR_HTIE)) ||
        (flag == DMA_FLAG_TC && (st->cr & SxCR_TCIE)) ||
        (flag == DMA_FLAG_TE && (st->cr & SxCR_TEIE)) ||
        (flag == DMA_FLAG_DME && (st->cr & SxCR_DMEIE))) {
        qemu_plugin_raise_irq(exc, false);
    }
}

static void dma_set_flag(DMAWithDMAMUX1State *s, int stream_idx, uint8_t flag)
{
    DMAStreamState *st = &s->stream[stream_idx];
    st->flags |= flag;
    dma_raise_stream_irq(s, stream_idx, flag);
}

static uint32_t dma_pack_group_flags(DMAStreamState *st, unsigned base)
{
    uint32_t v = 0;

    if (st->flags & DMA_FLAG_FE)  { v |= (1u << (base + 0)); }
    if (st->flags & DMA_FLAG_DME) { v |= (1u << (base + 2)); }
    if (st->flags & DMA_FLAG_TE)  { v |= (1u << (base + 3)); }
    if (st->flags & DMA_FLAG_HT)  { v |= (1u << (base + 4)); }
    if (st->flags & DMA_FLAG_TC)  { v |= (1u << (base + 5)); }

    return v;
}

static void dma_clear_group_flags(DMAStreamState *st, uint32_t value, unsigned base)
{
    if (value & (1u << (base + 0))) { st->flags &= ~DMA_FLAG_FE; }
    if (value & (1u << (base + 2))) { st->flags &= ~DMA_FLAG_DME; }
    if (value & (1u << (base + 3))) { st->flags &= ~DMA_FLAG_TE; }
    if (value & (1u << (base + 4))) { st->flags &= ~DMA_FLAG_HT; }
    if (value & (1u << (base + 5))) { st->flags &= ~DMA_FLAG_TC; }
}

static uint32_t dma_lisr_value(DMAWithDMAMUX1State *s)
{
    static const unsigned base_pos[4] = { 0, 6, 16, 22 };
    uint32_t v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        v |= dma_pack_group_flags(&s->stream[i], base_pos[i]);
    }
    return v;
}

static uint32_t dma_hisr_value(DMAWithDMAMUX1State *s)
{
    static const unsigned base_pos[4] = { 0, 6, 16, 22 };
    uint32_t v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        v |= dma_pack_group_flags(&s->stream[i + 4], base_pos[i]);
    }
    return v;
}

static void dma_clear_lifcr(DMAWithDMAMUX1State *s, uint32_t value)
{
    static const unsigned base_pos[4] = { 0, 6, 16, 22 };
    int i;

    for (i = 0; i < 4; i++) {
        dma_clear_group_flags(&s->stream[i], value, base_pos[i]);
    }
}

static void dma_clear_hifcr(DMAWithDMAMUX1State *s, uint32_t value)
{
    static const unsigned base_pos[4] = { 0, 6, 16, 22 };
    int i;

    for (i = 0; i < 4; i++) {
        dma_clear_group_flags(&s->stream[i + 4], value, base_pos[i]);
    }
}

static void dma_transfer_advance(DMAWithDMAMUX1State *s, int stream_idx,
                                 unsigned psize, unsigned msize)
{
    DMAStreamState *st = &s->stream[stream_idx];
    uint32_t old_ndtr = st->ndtr;
    uint32_t *cur_mem = dma_current_mem_ptr(st);

    if (old_ndtr == 0) {
        if ((st->cr & SxCR_CIRC) && st->initial_ndtr != 0) {
            st->ndtr = st->initial_ndtr;
            old_ndtr = st->ndtr;
            dma_reload_runtime(st);
            cur_mem = dma_current_mem_ptr(st);
        } else {
            return;
        }
    }

    if (st->cr & SxCR_PINC) {
        st->cur_par += psize;
    }
    if (st->cr & SxCR_MINC) {
        *cur_mem += msize;
    }

    st->ndtr = old_ndtr - 1;

    if (st->initial_ndtr > 1 && st->ndtr == (st->initial_ndtr / 2)) {
        dma_set_flag(s, stream_idx, DMA_FLAG_HT);
    }

    if (st->ndtr == 0) {
        dma_set_flag(s, stream_idx, DMA_FLAG_TC);

        if (st->cr & SxCR_DBM) {
            st->cr ^= SxCR_CT;
        }

        if (st->cr & SxCR_CIRC) {
            st->ndtr = st->initial_ndtr;
            dma_reload_runtime(st);
        } else {
            st->cr &= ~SxCR_EN;
        }
    }
}

static int dma_do_payload_p2m(DMAWithDMAMUX1State *s, int stream_idx,
                              const uint8_t *data, int len)
{
    DMAStreamState *st = &s->stream[stream_idx];
    uint8_t out[4] = {0, 0, 0, 0};
    uint32_t *cur_mem = dma_current_mem_ptr(st);
    unsigned psize = dma_psize_bytes(st);
    unsigned msize = dma_msize_bytes(st);
    unsigned copy_in = (unsigned)((len < 0) ? 0 : len);
    unsigned copy = psize;

    if (!data) {
        return 0;
    }

    if (copy_in < copy) {
        copy = copy_in;
    }
    if (copy > sizeof(out)) {
        copy = sizeof(out);
    }

    memcpy(out, data, copy);

    if (qemu_plugin_write_memory((unsigned long long)(*cur_mem), out, (int)msize) != (int)msize) {
        dma_set_flag(s, stream_idx, DMA_FLAG_TE);
        return 0;
    }

    dma_transfer_advance(s, stream_idx, psize, msize);
    return 1;
}

static int dma_do_nopayload_transfer(DMAWithDMAMUX1State *s, int stream_idx)
{
    DMAStreamState *st = &s->stream[stream_idx];
    uint8_t buf[4] = {0, 0, 0, 0};
    uint32_t *cur_mem = dma_current_mem_ptr(st);
    unsigned psize = dma_psize_bytes(st);
    unsigned msize = dma_msize_bytes(st);
    int dir = dma_dir(st);

    if (dir == 0) {
        /* Peripheral-to-memory.
         * This only succeeds if PAR points to RAM. MMIO reads are blocked by framework.
         */
        if (qemu_plugin_read_memory((unsigned long long)st->cur_par, buf, (int)psize) != (int)psize) {
            dma_set_flag(s, stream_idx, DMA_FLAG_TE);
            return 0;
        }
        if (qemu_plugin_write_memory((unsigned long long)(*cur_mem), buf, (int)msize) != (int)msize) {
            dma_set_flag(s, stream_idx, DMA_FLAG_TE);
            return 0;
        }
    } else if (dir == 1) {
        /* Memory-to-peripheral.
         * Only works if destination PAR is RAM; MMIO writes are blocked by framework.
         */
        if (qemu_plugin_read_memory((unsigned long long)(*cur_mem), buf, (int)msize) != (int)msize) {
            dma_set_flag(s, stream_idx, DMA_FLAG_TE);
            return 0;
        }
        if (qemu_plugin_write_memory((unsigned long long)st->cur_par, buf, (int)psize) != (int)psize) {
            dma_set_flag(s, stream_idx, DMA_FLAG_TE);
            return 0;
        }
    } else if (dir == 2) {
        /* Memory-to-memory: PAR is source, MxAR is destination */
        if (qemu_plugin_read_memory((unsigned long long)st->cur_par, buf, (int)psize) != (int)psize) {
            dma_set_flag(s, stream_idx, DMA_FLAG_TE);
            return 0;
        }
        if (qemu_plugin_write_memory((unsigned long long)(*cur_mem), buf, (int)msize) != (int)msize) {
            dma_set_flag(s, stream_idx, DMA_FLAG_TE);
            return 0;
        }
    } else {
        dma_set_flag(s, stream_idx, DMA_FLAG_TE);
        return 0;
    }

    dma_transfer_advance(s, stream_idx, psize, msize);
    return 1;
}

static int dma_service_stream(DMAWithDMAMUX1State *s, int stream_idx,
                              const uint8_t *data, int len, bool has_data)
{
    DMAStreamState *st;

    if (stream_idx < 0 || stream_idx >= DMA1_NUM_STREAMS) {
        return 0;
    }

    st = &s->stream[stream_idx];

    if (!(st->cr & SxCR_EN)) {
        return 0;
    }

    if (st->initial_ndtr == 0 && st->ndtr != 0) {
        st->initial_ndtr = st->ndtr;
    }

    if (st->ndtr == 0) {
        if ((st->cr & SxCR_CIRC) && st->initial_ndtr != 0) {
            st->ndtr = st->initial_ndtr;
            dma_reload_runtime(st);
        } else {
            return 0;
        }
    }

    if (has_data && dma_dir(st) == 0) {
        return dma_do_payload_p2m(s, stream_idx, data, len);
    }

    return dma_do_nopayload_transfer(s, stream_idx);
}

static void dma_route_request(DMAWithDMAMUX1State *s, int id,
                              const uint8_t *data, int len, bool has_data)
{
    int ch;
    int serviced = 0;

    /* First, route by DMAMUX request ID for DMA1 channels 0..7. */
    for (ch = 0; ch < DMA1_NUM_STREAMS; ch++) {
        uint32_t cxcr = s->dmamux_cxcr[ch];
        uint32_t req_id = cxcr & 0x7Fu;

        if (cxcr != 0 && req_id == (uint32_t)id) {
            serviced |= dma_service_stream(s, ch, data, len, has_data);
        }
    }

    /* Fallback: direct logical stream ID handling. This makes the model usable
     * even if the producer targets stream numbers directly rather than DMAMUX IDs.
     */
    if (!serviced && id >= 0 && id < DMA1_NUM_STREAMS) {
        dma_service_stream(s, id, data, len, has_data);
    }
}

static void dma_api_req_cb(void *opaque)
{
    DMAReqCtx *ctx = (DMAReqCtx *)opaque;
    dma_route_request(ctx->s, ctx->id, NULL, 0, false);
}

static void dma_api_req_data_cb(void *opaque, const uint8_t *data, int len)
{
    DMAReqCtx *ctx = (DMAReqCtx *)opaque;
    dma_route_request(ctx->s, ctx->id, data, len, true);
}

uint64_t dma_with_dmamux1_read(void *opaque, hwaddr addr, unsigned size)
{
    DMAWithDMAMUX1State *s = (DMAWithDMAMUX1State *)opaque;

    if (addr >= DMA1_BASE && addr < (DMA1_BASE + 0x400)) {
        hwaddr off = addr - DMA1_BASE;

        switch (off & ~0x3ULL) {
        case DMA_LISR_OFF:
            return reg_extract(dma_lisr_value(s), off, size);
        case DMA_HISR_OFF:
            return reg_extract(dma_hisr_value(s), off, size);
        case DMA_LIFCR_OFF:
        case DMA_HIFCR_OFF:
            return 0;
        default:
            break;
        }

        if (off >= DMA_STREAM_BASE) {
            hwaddr so = off - DMA_STREAM_BASE;
            int stream_idx = (int)(so / DMA_STREAM_STRIDE);
            hwaddr roff = so % DMA_STREAM_STRIDE;
            DMAStreamState *st;

            if (stream_idx >= 0 && stream_idx < DMA1_NUM_STREAMS) {
                st = &s->stream[stream_idx];
                switch (roff & ~0x3ULL) {
                case DMA_SXCR_OFF:
                    return reg_extract(st->cr, roff, size);
                case DMA_SXNDTR_OFF:
                    return reg_extract(st->ndtr, roff, size);
                case DMA_SXPAR_OFF:
                    return reg_extract(st->par, roff, size);
                case DMA_SXM0AR_OFF:
                    return reg_extract(st->m0ar, roff, size);
                case DMA_SXM1AR_OFF:
                    return reg_extract(st->m1ar, roff, size);
                case DMA_SXFCR_OFF:
                    return reg_extract(st->fcr, roff, size);
                default:
                    return 0;
                }
            }
        }

        return 0;
    }

    if (addr >= DMAMUX1_BASE && addr < (DMAMUX1_BASE + 0x100)) {
        hwaddr off = addr - DMAMUX1_BASE;

        if (off < (DMAMUX1_NUM_CHANNELS * DMAMUX_CXCR_STRIDE)) {
            int ch = (int)(off / DMAMUX_CXCR_STRIDE);
            hwaddr roff = off % DMAMUX_CXCR_STRIDE;
            return reg_extract(s->dmamux_cxcr[ch], roff, size);
        }

        switch (off & ~0x3ULL) {
        case DMAMUX_CSR_OFF:
            return reg_extract(s->dmamux_csr, off, size);
        case DMAMUX_CFR_OFF:
            return 0;
        default:
            return 0;
        }
    }

    return 0;
}

void dma_with_dmamux1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    DMAWithDMAMUX1State *s = (DMAWithDMAMUX1State *)opaque;

    if (addr >= DMA1_BASE && addr < (DMA1_BASE + 0x400)) {
        hwaddr off = addr - DMA1_BASE;

        switch (off & ~0x3ULL) {
        case DMA_LIFCR_OFF:
            dma_clear_lifcr(s, reg_apply_write(0, off, value, size));
            return;
        case DMA_HIFCR_OFF:
            dma_clear_hifcr(s, reg_apply_write(0, off, value, size));
            return;
        default:
            break;
        }

        if (off >= DMA_STREAM_BASE) {
            hwaddr so = off - DMA_STREAM_BASE;
            int stream_idx = (int)(so / DMA_STREAM_STRIDE);
            hwaddr roff = so % DMA_STREAM_STRIDE;
            DMAStreamState *st;

            if (stream_idx < 0 || stream_idx >= DMA1_NUM_STREAMS) {
                return;
            }

            st = &s->stream[stream_idx];

            switch (roff & ~0x3ULL) {
            case DMA_SXCR_OFF: {
                uint32_t old_cr = st->cr;
                st->cr = reg_apply_write(st->cr, roff, value, size);

                if (!(old_cr & SxCR_EN) && (st->cr & SxCR_EN)) {
                    dma_start_stream(st);
                }
                return;
            }
            case DMA_SXNDTR_OFF:
                st->ndtr = reg_apply_write(st->ndtr, roff, value, size);
                if (!(st->cr & SxCR_EN)) {
                    st->initial_ndtr = st->ndtr;
                }
                return;
            case DMA_SXPAR_OFF:
                st->par = reg_apply_write(st->par, roff, value, size);
                if (!(st->cr & SxCR_EN)) {
                    st->cur_par = st->par;
                }
                return;
            case DMA_SXM0AR_OFF:
                st->m0ar = reg_apply_write(st->m0ar, roff, value, size);
                if (!(st->cr & SxCR_EN)) {
                    st->cur_m0ar = st->m0ar;
                }
                return;
            case DMA_SXM1AR_OFF:
                st->m1ar = reg_apply_write(st->m1ar, roff, value, size);
                if (!(st->cr & SxCR_EN)) {
                    st->cur_m1ar = st->m1ar;
                }
                return;
            case DMA_SXFCR_OFF:
                st->fcr = reg_apply_write(st->fcr, roff, value, size);
                return;
            default:
                return;
            }
        }

        return;
    }

    if (addr >= DMAMUX1_BASE && addr < (DMAMUX1_BASE + 0x100)) {
        hwaddr off = addr - DMAMUX1_BASE;

        if (off < (DMAMUX1_NUM_CHANNELS * DMAMUX_CXCR_STRIDE)) {
            int ch = (int)(off / DMAMUX_CXCR_STRIDE);
            hwaddr roff = off % DMAMUX_CXCR_STRIDE;
            s->dmamux_cxcr[ch] = reg_apply_write(s->dmamux_cxcr[ch], roff, value, size);
            return;
        }

        switch (off & ~0x3ULL) {
        case DMAMUX_CFR_OFF:
            s->dmamux_csr &= ~reg_apply_write(0, off, value, size);
            return;
        default:
            return;
        }
    }
}

/* MUST return &g_state — framework stores this and passes it as opaque to _read/_write */
void* dma_with_dmamux1_init(ConfigSection* model_info)
{
    DMAWithDMAMUX1State *s = &g_dma_with_dmamux1;
    int i;

    (void)model_info;

    memset(s, 0, sizeof(*s));

    for (i = 0; i < DMA1_NUM_STREAMS; i++) {
        /* Common STM32 DMA reset value for FCR is 0x21. */
        s->stream[i].fcr = 0x21;
    }

    for (i = 0; i < API_DMA_IDS; i++) {
        s->req_ctx[i].s = s;
        s->req_ctx[i].id = i;
        api_dma_register_stream(i, dma_api_req_cb, &s->req_ctx[i]);
        api_dma_register_stream_data(i, dma_api_req_data_cb, &s->req_ctx[i]);
    }

    return &g_dma_with_dmamux1;
}
