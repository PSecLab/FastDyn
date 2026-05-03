// Device Model for DMA_with_DMAMUX1

#include <device.h>
#include <boardrunner/vio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Inferred Register Functions:
// DMA1  0x40020000:
//   0x00 LISR    - low interrupt status register
//   0x04 HISR    - high interrupt status register
//   0x08 LIFCR   - low interrupt flag clear register
//   0x0C HIFCR   - high interrupt flag clear register
//   0x28 S1CR    - stream1 control register
//   0x2C S1NDTR  - stream1 number of data register
//   0x30 S1PAR   - stream1 peripheral address register
//   0x34 S1M0AR  - stream1 memory0 address register
//   0x38 S1M1AR  - stream1 memory1 address register
//   0x3C S1FCR   - stream1 FIFO control register
//
// DMAMUX1 0x40020800:
//   0x04 C1CR    - channel1 control register
//   0x80 CSR     - channel status register
//   0x84 CFR     - channel flag clear register

#define DMA1_BASE               0x40020000ULL
#define DMAMUX1_BASE            0x40020800ULL

/* DMA1 register offsets */
#define DMA1_LISR_OFF           0x00
#define DMA1_HISR_OFF           0x04
#define DMA1_LIFCR_OFF          0x08
#define DMA1_HIFCR_OFF          0x0C

#define DMA1_S1CR_OFF           0x28
#define DMA1_S1NDTR_OFF         0x2C
#define DMA1_S1PAR_OFF          0x30
#define DMA1_S1M0AR_OFF         0x34
#define DMA1_S1M1AR_OFF         0x38
#define DMA1_S1FCR_OFF          0x3C

/* DMAMUX1 register offsets */
#define DMAMUX1_C1CR_OFF        0x04
#define DMAMUX1_CSR_OFF         0x80
#define DMAMUX1_CFR_OFF         0x84

/* Stream1 flag bits in LISR / LIFCR */
#define DMA_FLAG_FEIF1          (1U << 6)
#define DMA_FLAG_DMEIF1         (1U << 8)
#define DMA_FLAG_TEIF1          (1U << 9)
#define DMA_FLAG_HTIF1          (1U << 10)
#define DMA_FLAG_TCIF1          (1U << 11)
#define DMA_STREAM1_ALL_FLAGS   (DMA_FLAG_FEIF1 | DMA_FLAG_DMEIF1 | DMA_FLAG_TEIF1 | DMA_FLAG_HTIF1 | DMA_FLAG_TCIF1)

/* S1CR bits used here */
#define DMA_SXCR_EN             (1U << 0)
#define DMA_SXCR_DMEIE          (1U << 1)
#define DMA_SXCR_TEIE           (1U << 2)
#define DMA_SXCR_HTIE           (1U << 3)
#define DMA_SXCR_TCIE           (1U << 4)
#define DMA_SXCR_CIRC           (1U << 8)
#define DMA_SXCR_MINC           (1U << 10)
#define DMA_SXCR_PSIZE_SHIFT    11
#define DMA_SXCR_MSIZE_SHIFT    13
#define DMA_SXCR_PSIZE_MASK     (3U << DMA_SXCR_PSIZE_SHIFT)
#define DMA_SXCR_MSIZE_MASK     (3U << DMA_SXCR_MSIZE_SHIFT)

/* DMA1_Stream1_IRQn = 12 -> framework wants IRQ+16 */
#define DMA1_STREAM1_VECTOR     28

/* Optional framework DMA stream id for stream1 payload injection */
#define DMA1_STREAM1_ID         1

typedef struct DMA_with_DMAMUX1State {
    /* DMA1 global registers */
    uint32_t lisr;
    uint32_t hisr;

    /* DMA1 stream1 registers */
    uint32_t s1cr;
    uint32_t s1ndtr;
    uint32_t s1par;
    uint32_t s1m0ar;
    uint32_t s1m1ar;
    uint32_t s1fcr;

    /* DMAMUX1 registers */
    uint32_t c1cr;
    uint32_t csr;

    /* Emulation helpers */
    uint64_t periodic_timer;
    uint32_t sample_counter;
} DMA_with_DMAMUX1State;

static DMA_with_DMAMUX1State g_dma_with_dmamux1;

static bool addr_is_dma1(hwaddr addr) {
    return (addr >= DMA1_BASE) && (addr < DMA1_BASE + 0x400);
}

static bool addr_is_dmamux1(hwaddr addr) {
    return (addr >= DMAMUX1_BASE) && (addr < DMAMUX1_BASE + 0x200);
}

static int dma_stream1_item_size(DMA_with_DMAMUX1State *s) {
    uint32_t msize = (s->s1cr & DMA_SXCR_MSIZE_MASK) >> DMA_SXCR_MSIZE_SHIFT;
    switch (msize) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 4;
    default: return 4;
    }
}

static bool dma_stream1_enabled(DMA_with_DMAMUX1State *s) {
    uint32_t req_id = s->c1cr & 0x7f;
    return (s->s1cr & DMA_SXCR_EN) && (req_id != 0);
}

static void dma_stream1_fill_memory(DMA_with_DMAMUX1State *s) {
    int item_size;
    uint32_t count;
    uint64_t base;
    bool minc;
    uint32_t i;

    if (s->s1m0ar == 0 || s->s1ndtr == 0) {
        return;
    }

    item_size = dma_stream1_item_size(s);
    count = s->s1ndtr;
    base = s->s1m0ar;
    minc = !!(s->s1cr & DMA_SXCR_MINC);

    for (i = 0; i < count; i++) {
        uint32_t sample = (s->sample_counter++) & 0x0fff;
        uint8_t buf[4] = {0, 0, 0, 0};
        uint64_t dst = base + (minc ? ((uint64_t)i * (uint64_t)item_size) : 0);

        switch (item_size) {
        case 1:
            buf[0] = (uint8_t)sample;
            qemu_plugin_write_memory(dst, buf, 1);
            break;
        case 2:
            buf[0] = (uint8_t)(sample & 0xff);
            buf[1] = (uint8_t)((sample >> 8) & 0xff);
            qemu_plugin_write_memory(dst, buf, 2);
            break;
        default:
            buf[0] = (uint8_t)(sample & 0xff);
            buf[1] = (uint8_t)((sample >> 8) & 0xff);
            buf[2] = 0;
            buf[3] = 0;
            qemu_plugin_write_memory(dst, buf, 4);
            break;
        }
    }
}

static void dma_stream1_raise_event(DMA_with_DMAMUX1State *s) {
    if (!dma_stream1_enabled(s)) {
        return;
    }

    /* Don't generate a new event until firmware clears the old one. */
    if (s->lisr & (DMA_FLAG_HTIF1 | DMA_FLAG_TCIF1)) {
        return;
    }

    dma_stream1_fill_memory(s);

    /* Trace shows both HTIF1 and TCIF1 visible in ISR reads. */
    s->lisr |= (DMA_FLAG_HTIF1 | DMA_FLAG_TCIF1);

    if (s->s1cr & (DMA_SXCR_HTIE | DMA_SXCR_TCIE)) {
        qemu_plugin_raise_irq(DMA1_STREAM1_VECTOR, false);
    }
}

static void dma_with_dmamux1_periodic_cb(void *opaque) {
    DMA_with_DMAMUX1State *s = (DMA_with_DMAMUX1State *)opaque;
    dma_stream1_raise_event(s);
}

/*
 * Optional external payload path:
 * another peripheral may submit bytes to DMA1 stream1 via framework DMA API.
 * We accept the payload, store it at the programmed memory target, then expose
 * the same HT/TC flags firmware expects.
 */
static void dma_with_dmamux1_stream1_req(void *opaque, const uint8_t *data, int len) {
    DMA_with_DMAMUX1State *s = (DMA_with_DMAMUX1State *)opaque;
    int max_len;
    int item_size;
    uint64_t dst;

    if (!dma_stream1_enabled(s)) {
        return;
    }
    if (s->s1m0ar == 0 || s->s1ndtr == 0 || len <= 0) {
        dma_stream1_raise_event(s);
        return;
    }

    item_size = dma_stream1_item_size(s);
    max_len = (int)(s->s1ndtr * (uint32_t)item_size);
    if (max_len <= 0) {
        dma_stream1_raise_event(s);
        return;
    }

    if (len > max_len) {
        len = max_len;
    }

    dst = s->s1m0ar;
    qemu_plugin_write_memory(dst, (uint8_t *)data, len);

    /* Keep trace-compatible flag behavior. */
    if (!(s->lisr & (DMA_FLAG_HTIF1 | DMA_FLAG_TCIF1))) {
        s->lisr |= (DMA_FLAG_HTIF1 | DMA_FLAG_TCIF1);
        if (s->s1cr & (DMA_SXCR_HTIE | DMA_SXCR_TCIE)) {
            qemu_plugin_raise_irq(DMA1_STREAM1_VECTOR, false);
        }
    }
}

// This function will emulate all device reads
uint64_t dma_with_dmamux1_read(void *opaque, hwaddr addr, unsigned size) {
    DMA_with_DMAMUX1State *s = (DMA_with_DMAMUX1State *)opaque;
    uint64_t val = 0;

    if (addr_is_dma1(addr)) {
        hwaddr offset = addr - DMA1_BASE;

        switch (offset) {
        case DMA1_LISR_OFF:
            val = s->lisr;
            break;
        case DMA1_HISR_OFF:
            val = s->hisr;
            break;
        case DMA1_S1CR_OFF:
            val = s->s1cr;
            break;
        case DMA1_S1NDTR_OFF:
            val = s->s1ndtr;
            break;
        case DMA1_S1PAR_OFF:
            val = s->s1par;
            break;
        case DMA1_S1M0AR_OFF:
            val = s->s1m0ar;
            break;
        case DMA1_S1M1AR_OFF:
            val = s->s1m1ar;
            break;
        case DMA1_S1FCR_OFF:
            val = s->s1fcr;
            break;
        default:
            val = 0;
            break;
        }
    } else if (addr_is_dmamux1(addr)) {
        hwaddr offset = addr - DMAMUX1_BASE;

        switch (offset) {
        case DMAMUX1_C1CR_OFF:
            val = s->c1cr;
            break;
        case DMAMUX1_CSR_OFF:
            val = s->csr;
            break;
        case DMAMUX1_CFR_OFF:
            val = 0; /* write-only clear register */
            break;
        default:
            val = 0;
            break;
        }
    } else {
        val = 0;
    }

    if (size == 1) {
        return val & 0xff;
    } else if (size == 2) {
        return val & 0xffff;
    }
    return val;
}

// This function will emulate all device writes
void dma_with_dmamux1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    DMA_with_DMAMUX1State *s = (DMA_with_DMAMUX1State *)opaque;
    uint32_t v = (uint32_t)value;

    (void)size;

    if (addr_is_dma1(addr)) {
        hwaddr offset = addr - DMA1_BASE;

        switch (offset) {
        case DMA1_LIFCR_OFF:
            /* Stream1 low-flag clear bits */
            if (v & DMA_FLAG_FEIF1) {
                s->lisr &= ~DMA_FLAG_FEIF1;
            }
            if (v & DMA_FLAG_DMEIF1) {
                s->lisr &= ~DMA_FLAG_DMEIF1;
            }
            if (v & DMA_FLAG_TEIF1) {
                s->lisr &= ~DMA_FLAG_TEIF1;
            }
            if (v & DMA_FLAG_HTIF1) {
                s->lisr &= ~DMA_FLAG_HTIF1;
            }
            if (v & DMA_FLAG_TCIF1) {
                s->lisr &= ~DMA_FLAG_TCIF1;
            }
            break;

        case DMA1_HIFCR_OFF:
            /* Not used in this trace. */
            s->hisr = 0;
            break;

        case DMA1_S1CR_OFF:
            s->s1cr = v;
            break;
        case DMA1_S1NDTR_OFF:
            s->s1ndtr = v;
            break;
        case DMA1_S1PAR_OFF:
            s->s1par = v;
            break;
        case DMA1_S1M0AR_OFF:
            s->s1m0ar = v;
            break;
        case DMA1_S1M1AR_OFF:
            s->s1m1ar = v;
            break;
        case DMA1_S1FCR_OFF:
            s->s1fcr = v;
            break;
        default:
            break;
        }
        return;
    }

    if (addr_is_dmamux1(addr)) {
        hwaddr offset = addr - DMAMUX1_BASE;

        switch (offset) {
        case DMAMUX1_C1CR_OFF:
            s->c1cr = v;
            break;
        case DMAMUX1_CFR_OFF:
            /* W1C for channel status bits; trace uses 0x2. */
            s->csr &= ~v;
            break;
        default:
            break;
        }
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* dma_with_dmamux1_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_dma_with_dmamux1, 0, sizeof(g_dma_with_dmamux1));

    /* Reference-manual reset behavior relevant to observed stream1 use. */
    g_dma_with_dmamux1.s1fcr = 0x21;

    /* Optional framework DMA ingress for stream1. */
    api_dma_register_stream_data(DMA1_STREAM1_ID, dma_with_dmamux1_stream1_req, &g_dma_with_dmamux1);

    /* Periodic fallback to emulate recurring ADC-driven circular DMA activity. */
    g_dma_with_dmamux1.periodic_timer =
        qemu_plugin_timer_new_period_ns(dma_with_dmamux1_periodic_cb,
                                        &g_dma_with_dmamux1,
                                        1000000ULL); /* 1 ms virtual period */

    return &g_dma_with_dmamux1;
}