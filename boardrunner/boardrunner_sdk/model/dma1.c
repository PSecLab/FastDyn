#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>

#define DMA1_BASE_ADDR 0x40026000ULL

#define DMA_LISR_OFF  0x00
#define DMA_HISR_OFF  0x04
#define DMA_LIFCR_OFF 0x08
#define DMA_HIFCR_OFF 0x0C
#define DMA_S0_OFF    0x10
#define DMA_STREAM_STRIDE 0x18

#define DMA_SXCR_OFF   0x00
#define DMA_SXNDTR_OFF 0x04
#define DMA_SXPAR_OFF  0x08
#define DMA_SXM0AR_OFF 0x0C
#define DMA_SXM1AR_OFF 0x10
#define DMA_SXFCR_OFF  0x14

#define DMA_SXCR_EN        (1U << 0)
#define DMA_SXCR_TCIE      (1U << 4)
#define DMA_SXCR_DIR_MASK  (3U << 6)
#define DMA_SXCR_DIR_P2M   (0U << 6)
#define DMA_SXCR_DIR_M2P   (1U << 6)
#define DMA_SXCR_MINC      (1U << 10)

#define DMA1_STREAM_SPI2_RX 3U
#define DMA1_STREAM_SPI2_TX 4U

#define DMA1_STREAM3_IRQN 14
#define DMA1_STREAM4_IRQN 15

#define SPI2_DR_ADDR 0x4000380CULL

typedef struct {
    uint32_t cr;
    uint32_t ndtr;
    uint32_t par;
    uint32_t m0ar;
    uint32_t m1ar;
    uint32_t fcr;
} DMA1StreamState;

typedef struct {
    uint32_t lisr;
    uint32_t hisr;
    bool stream4_busy;
    DMA1StreamState stream[8];
} DMA1State;

static DMA1State g_dma1_state;

static void dma1_kick_stream4_tx(DMA1State *s);

static uint32_t dma1_mask_for_size(unsigned size) {
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    default:
        return 0xFFFFFFFFU;
    }
}

static uint32_t dma1_extract_reg32(uint32_t reg, uint64_t suboff, unsigned size) {
    uint32_t mask = dma1_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);

    if (size >= 4) {
        return reg;
    }
    return (reg >> shift) & mask;
}

static uint32_t dma1_merge_reg32(uint32_t oldv, uint64_t suboff, uint64_t value, unsigned size) {
    uint32_t mask = dma1_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);
    uint32_t fullmask = (size >= 4) ? 0xFFFFFFFFU : (mask << shift);

    if (size >= 4) {
        return (uint32_t)value;
    }
    return (oldv & ~fullmask) | ((((uint32_t)value) & mask) << shift);
}

static bool dma1_stream_targets_spi2(const DMA1StreamState *st) {
    return ((uint64_t)st->par) == SPI2_DR_ADDR;
}

static bool dma1_stream3_rx_ready_for_spi2(const DMA1State *s) {
    const DMA1StreamState *st = &s->stream[DMA1_STREAM_SPI2_RX];

    return ((st->cr & DMA_SXCR_EN) != 0) &&
           ((st->cr & DMA_SXCR_DIR_MASK) == DMA_SXCR_DIR_P2M) &&
           dma1_stream_targets_spi2(st) &&
           (st->m0ar != 0U) &&
           (st->ndtr != 0U);
}

static void dma1_set_tc_flag(DMA1State *s, uint32_t stream_idx) {
    switch (stream_idx) {
    case 0:
        s->lisr |= (1U << 5);
        break;
    case 1:
        s->lisr |= (1U << 11);
        break;
    case 2:
        s->lisr |= (1U << 21);
        break;
    case 3:
        s->lisr |= (1U << 27);
        break;
    case 4:
        s->hisr |= (1U << 5);
        break;
    case 5:
        s->hisr |= (1U << 11);
        break;
    case 6:
        s->hisr |= (1U << 21);
        break;
    case 7:
        s->hisr |= (1U << 27);
        break;
    default:
        break;
    }
}

static void dma1_raise_stream_irq(const DMA1StreamState *st, uint32_t stream_idx) {
    if ((st->cr & DMA_SXCR_TCIE) == 0) {
        return;
    }

    switch (stream_idx) {
    case DMA1_STREAM_SPI2_RX:
        qemu_plugin_raise_irq(DMA1_STREAM3_IRQN + 16, false);
        break;
    case DMA1_STREAM_SPI2_TX:
        qemu_plugin_raise_irq(DMA1_STREAM4_IRQN + 16, false);
        break;
    default:
        break;
    }
}

static void dma1_complete_stream(DMA1State *s, uint32_t stream_idx) {
    DMA1StreamState *st;

    if (stream_idx >= 8U) {
        return;
    }

    st = &s->stream[stream_idx];
    st->cr &= ~DMA_SXCR_EN;
    dma1_set_tc_flag(s, stream_idx);
    dma1_raise_stream_irq(st, stream_idx);
}

static void dma1_stream3_rx_data(void *opaque, const uint8_t *data, int len) {
    DMA1State *s = (DMA1State *)opaque;
    DMA1StreamState *st;
    uint8_t tmp[8];
    int chunk;
    int i;

    if (s == NULL) {
        s = &g_dma1_state;
    }
    if (data == NULL || len <= 0) {
        return;
    }

    st = &s->stream[DMA1_STREAM_SPI2_RX];

    if ((st->cr & DMA_SXCR_EN) == 0 ||
        (st->cr & DMA_SXCR_DIR_MASK) != DMA_SXCR_DIR_P2M ||
        !dma1_stream_targets_spi2(st) ||
        st->ndtr == 0) {
        return;
    }

    chunk = len;
    if ((uint32_t)chunk > st->ndtr) {
        chunk = (int)st->ndtr;
    }
    if (chunk > (int)sizeof(tmp)) {
        chunk = (int)sizeof(tmp);
    }

    if ((st->cr & DMA_SXCR_MINC) != 0) {
        memcpy(tmp, data, (unsigned)chunk);
        if (qemu_plugin_write_memory(st->m0ar, tmp, chunk) != 0) {
            dev_debug("dma1: SPI2 RX DMA write failed");
        }
        st->m0ar += (uint32_t)chunk;
    } else {
        for (i = 0; i < chunk; i++) {
            tmp[0] = data[i];
            if (qemu_plugin_write_memory(st->m0ar, tmp, 1) != 0) {
                dev_debug("dma1: SPI2 RX DMA byte write failed");
                break;
            }
        }
    }

    if ((uint32_t)chunk <= st->ndtr) {
        st->ndtr -= (uint32_t)chunk;
    } else {
        st->ndtr = 0;
    }

    if (st->ndtr == 0) {
        dma1_complete_stream(s, DMA1_STREAM_SPI2_RX);
    }
}

static void dma1_kick_stream4_tx(DMA1State *s) {
    DMA1StreamState *st = &s->stream[DMA1_STREAM_SPI2_TX];

    if ((st->cr & DMA_SXCR_EN) == 0 ||
        (st->cr & DMA_SXCR_DIR_MASK) != DMA_SXCR_DIR_M2P ||
        !dma1_stream_targets_spi2(st) ||
        st->m0ar == 0U ||
        st->ndtr == 0U) {
        return;
    }

    while (((st->cr & DMA_SXCR_EN) != 0) && st->ndtr != 0) {
        uint8_t buf[8];
        int chunk = (st->ndtr > sizeof(buf)) ? (int)sizeof(buf) : (int)st->ndtr;
        int rc;

        if ((st->cr & DMA_SXCR_MINC) != 0) {
            if (qemu_plugin_read_memory(st->m0ar, buf, chunk) != 0) {
                dev_debug("dma1: SPI2 TX DMA read failed");
                memset(buf, 0xFF, (unsigned)chunk);
            }
            st->m0ar += (uint32_t)chunk;
        } else {
            if (qemu_plugin_read_memory(st->m0ar, buf, 1) != 0) {
                dev_debug("dma1: SPI2 TX DMA byte read failed");
                buf[0] = 0xFF;
            }
            memset(&buf[1], buf[0], (unsigned)(chunk - 1));
        }

        rc = api_dma_request_data(1, DMA1_STREAM_SPI2_TX, st->par, buf, chunk);
        if (rc < 0) {
            dev_debug("dma1: SPI2 TX DMA submission failed");
            return;
        }

        st->ndtr -= (uint32_t)chunk;
    }

    if (st->ndtr == 0) {
        dma1_complete_stream(s, DMA1_STREAM_SPI2_TX);
    }
}

static void dma1_try_kick_stream4_tx(DMA1State *s) {
    DMA1StreamState *st;

    if (s == NULL || s->stream4_busy) {
        return;
    }

    st = &s->stream[DMA1_STREAM_SPI2_TX];

    if ((st->cr & DMA_SXCR_EN) == 0 ||
        (st->cr & DMA_SXCR_DIR_MASK) != DMA_SXCR_DIR_M2P ||
        !dma1_stream_targets_spi2(st) ||
        st->m0ar == 0U ||
        st->ndtr == 0U) {
        return;
    }

    /*
     * ChibiOS programs both SPI2 RX and TX streams for each exchange.
     * Defer the TX side until the matching RX stream is armed so the
     * response bytes from RDID/READ are not lost due to ordering.
     */
    if (!dma1_stream3_rx_ready_for_spi2(s)) {
        return;
    }

    s->stream4_busy = true;
    dma1_kick_stream4_tx(s);
    s->stream4_busy = false;
}

void* dma1_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_dma1_state, 0, sizeof(g_dma1_state));
    api_dma_register_stream_data(1, DMA1_STREAM_SPI2_RX, dma1_stream3_rx_data, &g_dma1_state);
    return &g_dma1_state;
}

uint64_t dma1_read(void *opaque, uint64_t addr, unsigned size) {
    DMA1State *s = (DMA1State *)opaque;
    uint64_t offset = addr - DMA1_BASE_ADDR;

    if (s == NULL) {
        s = &g_dma1_state;
    }

    switch (offset & ~0x3ULL) {
    case DMA_LISR_OFF:
        return dma1_extract_reg32(s->lisr, offset - DMA_LISR_OFF, size);
    case DMA_HISR_OFF:
        return dma1_extract_reg32(s->hisr, offset - DMA_HISR_OFF, size);
    default:
        break;
    }

    if (offset >= DMA_S0_OFF && offset < (DMA_S0_OFF + (8U * DMA_STREAM_STRIDE))) {
        uint32_t stream_idx = (uint32_t)((offset - DMA_S0_OFF) / DMA_STREAM_STRIDE);
        uint32_t reg_off = (uint32_t)((offset - DMA_S0_OFF) % DMA_STREAM_STRIDE);
        DMA1StreamState *st = &s->stream[stream_idx];

        switch (reg_off & ~0x3U) {
        case DMA_SXCR_OFF:
            return dma1_extract_reg32(st->cr, reg_off - DMA_SXCR_OFF, size);
        case DMA_SXNDTR_OFF:
            return dma1_extract_reg32(st->ndtr, reg_off - DMA_SXNDTR_OFF, size);
        case DMA_SXPAR_OFF:
            return dma1_extract_reg32(st->par, reg_off - DMA_SXPAR_OFF, size);
        case DMA_SXM0AR_OFF:
            return dma1_extract_reg32(st->m0ar, reg_off - DMA_SXM0AR_OFF, size);
        case DMA_SXM1AR_OFF:
            return dma1_extract_reg32(st->m1ar, reg_off - DMA_SXM1AR_OFF, size);
        case DMA_SXFCR_OFF:
            return dma1_extract_reg32(st->fcr, reg_off - DMA_SXFCR_OFF, size);
        default:
            return 0;
        }
    }

    return 0;
}

void dma1_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    DMA1State *s = (DMA1State *)opaque;
    uint64_t offset = addr - DMA1_BASE_ADDR;

    if (s == NULL) {
        s = &g_dma1_state;
    }

    switch (offset & ~0x3ULL) {
    case DMA_LIFCR_OFF:
        s->lisr &= ~((uint32_t)value);
        return;
    case DMA_HIFCR_OFF:
        s->hisr &= ~((uint32_t)value);
        return;
    default:
        break;
    }

    if (offset >= DMA_S0_OFF && offset < (DMA_S0_OFF + (8U * DMA_STREAM_STRIDE))) {
        uint32_t stream_idx = (uint32_t)((offset - DMA_S0_OFF) / DMA_STREAM_STRIDE);
        uint32_t reg_off = (uint32_t)((offset - DMA_S0_OFF) % DMA_STREAM_STRIDE);
        DMA1StreamState *st = &s->stream[stream_idx];

        switch (reg_off & ~0x3U) {
        case DMA_SXCR_OFF:
            st->cr = dma1_merge_reg32(st->cr, reg_off - DMA_SXCR_OFF, value, size);
            if (stream_idx == DMA1_STREAM_SPI2_RX || stream_idx == DMA1_STREAM_SPI2_TX) {
                dma1_try_kick_stream4_tx(s);
            }
            return;
        case DMA_SXNDTR_OFF:
            st->ndtr = dma1_merge_reg32(st->ndtr, reg_off - DMA_SXNDTR_OFF, value, size);
            if (stream_idx == DMA1_STREAM_SPI2_RX || stream_idx == DMA1_STREAM_SPI2_TX) {
                dma1_try_kick_stream4_tx(s);
            }
            return;
        case DMA_SXPAR_OFF:
            st->par = dma1_merge_reg32(st->par, reg_off - DMA_SXPAR_OFF, value, size);
            if (stream_idx == DMA1_STREAM_SPI2_RX || stream_idx == DMA1_STREAM_SPI2_TX) {
                dma1_try_kick_stream4_tx(s);
            }
            return;
        case DMA_SXM0AR_OFF:
            st->m0ar = dma1_merge_reg32(st->m0ar, reg_off - DMA_SXM0AR_OFF, value, size);
            if (stream_idx == DMA1_STREAM_SPI2_RX || stream_idx == DMA1_STREAM_SPI2_TX) {
                dma1_try_kick_stream4_tx(s);
            }
            return;
        case DMA_SXM1AR_OFF:
            st->m1ar = dma1_merge_reg32(st->m1ar, reg_off - DMA_SXM1AR_OFF, value, size);
            return;
        case DMA_SXFCR_OFF:
            st->fcr = dma1_merge_reg32(st->fcr, reg_off - DMA_SXFCR_OFF, value, size);
            return;
        default:
            return;
        }
    }
}