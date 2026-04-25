// Device Model for SPI1
//
// Inferred Register Functions:
//   0x00 CR1   : control (SPE, CSTART observed)
//   0x04 CR2   : transfer size (TSIZE observed as 2)
//   0x08 CFG1  : format/config (DSIZE low bits observed as 7 => 8-bit)
//   0x0C CFG2  : mode/config
//   0x10 IER   : interrupt enable
//   0x14 SR    : synthesized status
//   0x18 IFCR  : flag clear, reads as 0
//   0x20 TXDR  : transmit data -> SPI bus transfer
//   0x30 RXDR  : receive data from internal RX FIFO
//   0x50 CGFR  : config register, plain RW
//
// Notes:
// - Absolute MMIO addresses are passed in; this model subtracts SPI1 base.
// - The model uses the framework SPI bus API and assumes the first configured
//   SPI slave CS line is the active one controlled by this peripheral.
// - Status register synthesis is intentionally minimal and trace-driven.

#include <device.h>
#include <boardrunner/vio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define SPI1_BASE 0x40013000ULL

#define SPI1_CR1   0x00
#define SPI1_CR2   0x04
#define SPI1_CFG1  0x08
#define SPI1_CFG2  0x0C
#define SPI1_IER   0x10
#define SPI1_SR    0x14
#define SPI1_IFCR  0x18
#define SPI1_TXDR  0x20
#define SPI1_RXDR  0x30
#define SPI1_CGFR  0x50

/* CR1 bits used */
#define SPI1_CR1_SPE       (1u << 0)
#define SPI1_CR1_CSTART    (1u << 9)

/* SR bits used/synthesized */
#define SPI1_SR_RXP        (1u << 0)
#define SPI1_SR_TXP        (1u << 1)
#define SPI1_SR_DXP        (1u << 2)
#define SPI1_SR_EOT        (1u << 3)
#define SPI1_SR_TXTF       (1u << 4)
#define SPI1_SR_TXC        (1u << 12)
#define SPI1_SR_RXPLVL_1P  (1u << 13)   /* minimal encoding used by trace */
#define SPI1_SR_CTSIZE_SHIFT 16

#define SPI1_RX_FIFO_SIZE 16

typedef struct {
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cfg1;
    uint32_t cfg2;
    uint32_t ier;
    uint32_t cgfr;

    uint32_t last_txdr;
    uint32_t last_ifcr_write;
    uint32_t visible_ctsize;

    SPIBus bus;
    int active_cs;
    int cs_asserted;

    uint8_t rx_fifo[SPI1_RX_FIFO_SIZE];
    int rx_head;
    int rx_count;

    int started;
    int transfer_seen;
    int completed;
} SPI1State;

static SPI1State g_spi1;

static uint64_t spi1_mask_by_size(uint64_t v, unsigned size) {
    switch (size) {
        case 1: return v & 0xFFu;
        case 2: return v & 0xFFFFu;
        case 4: return v & 0xFFFFFFFFu;
        default: return v;
    }
}

static unsigned spi1_frame_bits(SPI1State *s) {
    /* STM32H7 DSIZE encoding: value N means N+1 bits. */
    unsigned dsize = s->cfg1 & 0x1Fu;
    unsigned bits = dsize + 1u;

    if (bits == 0 || bits > 32) {
        bits = 8;
    }
    return bits;
}

static unsigned spi1_frame_bytes(SPI1State *s) {
    unsigned bits = spi1_frame_bits(s);
    return (bits + 7u) / 8u;
}

static uint32_t spi1_tsize(SPI1State *s) {
    return s->cr2 & 0xFFFFu;
}

static void spi1_rx_fifo_reset(SPI1State *s) {
    s->rx_head = 0;
    s->rx_count = 0;
}

static void spi1_rx_fifo_push(SPI1State *s, uint8_t v) {
    if (s->rx_count >= SPI1_RX_FIFO_SIZE) {
        return;
    }
    s->rx_fifo[(s->rx_head + s->rx_count) % SPI1_RX_FIFO_SIZE] = v;
    s->rx_count++;
}

static uint8_t spi1_rx_fifo_pop(SPI1State *s) {
    uint8_t v = 0;
    if (s->rx_count <= 0) {
        return 0;
    }
    v = s->rx_fifo[s->rx_head];
    s->rx_head = (s->rx_head + 1) % SPI1_RX_FIFO_SIZE;
    s->rx_count--;
    return v;
}

static void spi1_set_cs_level(SPI1State *s, int level) {
    if (s->cs_asserted == !level) {
        return;
    }
    api_spi_set_cs(&s->bus, s->active_cs, level);
    s->cs_asserted = (level == 0) ? 1 : 0;
}

static void spi1_begin_transaction(SPI1State *s) {
    if (!s->started) {
        s->started = 1;
        spi1_rx_fifo_reset(s);
    }

    s->transfer_seen = 0;
    s->completed = 0;
    s->visible_ctsize = spi1_tsize(s);

    if (!s->cs_asserted) {
        spi1_set_cs_level(s, 0);
    }
}

static void spi1_finish_transaction_if_idle(SPI1State *s) {
    (void)s;
}

static void spi1_enqueue_response_frame(SPI1State *s, uint32_t resp) {
    unsigned frame_bytes = spi1_frame_bytes(s);
    unsigned i;

    if (frame_bytes == 0) {
        frame_bytes = 1;
    }
    if (frame_bytes > 4) {
        frame_bytes = 4;
    }

    for (i = 0; i < frame_bytes; i++) {
        spi1_rx_fifo_push(s, (uint8_t)((resp >> (8 * i)) & 0xFFu));
    }
}

static uint32_t spi1_build_sr(SPI1State *s) {
    uint32_t sr = 0;

    sr |= (s->visible_ctsize << SPI1_SR_CTSIZE_SHIFT);

    if ((s->cr1 & SPI1_CR1_SPE) && (s->started || (s->cr1 & SPI1_CR1_CSTART))) {
        sr |= SPI1_SR_TXP;
    }

    if (s->rx_count > 0) {
        sr |= SPI1_SR_RXP;
        sr |= SPI1_SR_DXP;
        sr |= SPI1_SR_RXPLVL_1P;
    }

    if (s->completed) {
        sr |= SPI1_SR_EOT | SPI1_SR_TXTF | SPI1_SR_TXC;
    }

    return sr;
}

static uint32_t spi1_read_rxdr(SPI1State *s) {
    unsigned frame_bytes = spi1_frame_bytes(s);
    uint32_t v = 0;
    unsigned i;

    if (frame_bytes == 0) {
        frame_bytes = 1;
    }
    if (frame_bytes > 4) {
        frame_bytes = 4;
    }

    for (i = 0; i < frame_bytes; i++) {
        if (s->rx_count <= 0) {
            break;
        }
        v |= ((uint32_t)spi1_rx_fifo_pop(s)) << (8 * i);
    }

    /*
     * Trace shows SR transitioning to the completed state while RX data may
     * still remain in FIFO (0x301F), then to a completed/no-RX state (0x101A)
     * after the final RXDR read. Model that by latching completion on the
     * first RXDR consumption rather than only after the FIFO fully drains.
     */
    if (s->transfer_seen && !s->completed) {
        s->completed = 1;
        s->visible_ctsize = 0;
    }

    spi1_finish_transaction_if_idle(s);
    return v;
}

static void spi1_write_txdr(SPI1State *s, uint64_t value, unsigned size) {
    uint64_t raw;
    unsigned bits;
    unsigned frame_bytes;
    unsigned access_bytes;
    unsigned frames;
    unsigned frame_idx;

    if (!(s->cr1 & SPI1_CR1_SPE)) {
        return;
    }

    if (!s->started && (s->cr1 & SPI1_CR1_CSTART)) {
        spi1_begin_transaction(s);
    } else if (!s->started) {
        /*
         * Be permissive: if firmware writes TXDR after enabling but without an
         * explicit new CSTART readback path in the trace, still open a transfer.
         */
        spi1_begin_transaction(s);
    }

    bits = spi1_frame_bits(s);
    frame_bytes = spi1_frame_bytes(s);
    if (frame_bytes == 0) {
        frame_bytes = 1;
    }

    raw = spi1_mask_by_size(value, size);
    access_bytes = size;
    if (access_bytes == 0) {
        access_bytes = 1;
    }

    frames = (access_bytes + frame_bytes - 1u) / frame_bytes;
    if (spi1_tsize(s) != 0 && frames > spi1_tsize(s)) {
        frames = spi1_tsize(s);
    }
    if (frames == 0) {
        frames = 1;
    }

    s->last_txdr = (uint32_t)spi1_mask_by_size(value, (size > 4) ? 4 : size);
    s->transfer_seen = 1;
    s->completed = 0;
    s->visible_ctsize = spi1_tsize(s);

    for (frame_idx = 0; frame_idx < frames; frame_idx++) {
        uint32_t tx_val = 0;
        uint32_t resp;
        unsigned byte_idx;
        unsigned base = frame_idx * frame_bytes;

        for (byte_idx = 0; byte_idx < frame_bytes && (base + byte_idx) < 8; byte_idx++) {
            tx_val |= ((uint32_t)((raw >> (8 * (base + byte_idx))) & 0xFFu)) << (8 * byte_idx);
        }

        if (bits < 32) {
            tx_val &= (uint32_t)((1u << bits) - 1u);
        }

        resp = api_spi_transfer(&s->bus, tx_val);
        spi1_enqueue_response_frame(s, resp);
    }
}

// This function will emulate all device reads
uint64_t spi1_read(void *opaque, hwaddr addr, unsigned size) {
    SPI1State *s = (SPI1State *)opaque;
    hwaddr offset = addr - SPI1_BASE;
    uint64_t ret = 0;

    switch (offset) {
        case SPI1_CR1:
            ret = s->cr1;
            break;
        case SPI1_CR2:
            ret = s->cr2;
            break;
        case SPI1_CFG1:
            ret = s->cfg1;
            break;
        case SPI1_CFG2:
            ret = s->cfg2;
            break;
        case SPI1_IER:
            ret = s->ier;
            break;
        case SPI1_SR:
            ret = spi1_build_sr(s);
            break;
        case SPI1_IFCR:
            /* Write-only in hardware; trace reads returned 0. */
            ret = 0;
            break;
        case SPI1_RXDR:
            ret = spi1_read_rxdr(s);
            break;
        case SPI1_CGFR:
            ret = s->cgfr;
            break;
        case SPI1_TXDR:
            ret = s->last_txdr;
            break;
        default:
            ret = 0;
            break;
    }

    return spi1_mask_by_size(ret, size);
}

// This function will emulate all device writes
void spi1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    SPI1State *s = (SPI1State *)opaque;
    hwaddr offset = addr - SPI1_BASE;
    uint32_t v = (uint32_t)spi1_mask_by_size(value, (size > 4) ? 4 : size);

    switch (offset) {
        case SPI1_CR1: {
            uint32_t old = s->cr1;
            s->cr1 = v;

            if (!(s->cr1 & SPI1_CR1_SPE)) {
                s->started = 0;
                s->transfer_seen = 0;
                s->completed = 0;
                s->visible_ctsize = 0;
                spi1_rx_fifo_reset(s);
                s->cr1 &= ~SPI1_CR1_CSTART;
                if (s->cs_asserted) {
                    spi1_set_cs_level(s, 1);
                }
                break;
            }

            if ((s->cr1 & SPI1_CR1_CSTART) && !(old & SPI1_CR1_CSTART)) {
                spi1_begin_transaction(s);
            }
            break;
        }

        case SPI1_CR2:
            s->cr2 = v;
            break;

        case SPI1_CFG1:
            s->cfg1 = v;
            break;

        case SPI1_CFG2:
            s->cfg2 = v;
            break;

        case SPI1_IER:
            s->ier = v;
            break;

        case SPI1_IFCR:
            /*
             * Hardware reads as 0, but firmware clears EOT/TXTF-style status here.
             * The trace indicates this does not disable the peripheral or require
             * a new CR1/CSTART write before the next TXDR; it simply re-arms the
             * status back to the pre-transfer state (e.g. SR=0x20002).
             */
            s->last_ifcr_write = v;
            s->completed = 0;
            if (s->rx_count == 0 && (s->started || (s->cr1 & SPI1_CR1_CSTART))) {
                s->transfer_seen = 0;
                s->visible_ctsize = spi1_tsize(s);
            }
            break;

        case SPI1_TXDR:
            spi1_write_txdr(s, value, size);
            break;

        case SPI1_CGFR:
            s->cgfr = v;
            break;

        case SPI1_RXDR:
            /* RXDR is read-only from firmware perspective; ignore writes. */
            break;

        default:
            /* Unhandled register: ignore write to stay permissive. */
            break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* spi1_init(ConfigSection* model_info) {
    int i;

    memset(&g_spi1, 0, sizeof(g_spi1));

    g_spi1.bus = api_spi_init_bus(model_info);

    if (g_spi1.bus.Slaves.num_slaves > 0) {
        g_spi1.active_cs = g_spi1.bus.Slaves.slave[0].cs_id;
    } else {
        g_spi1.active_cs = 0;
    }

    for (i = 0; i < NUM_CS_LINES; i++) {
        api_spi_set_cs(&g_spi1.bus, i, 1);
    }

    g_spi1.cs_asserted = 0;
    g_spi1.rx_head = 0;
    g_spi1.rx_count = 0;
    g_spi1.visible_ctsize = 0;

    /* Reset values inferred from trace. */
    g_spi1.cr1 = 0x00000000;
    g_spi1.cr2 = 0x00000000;
    g_spi1.cfg1 = 0x00000000;
    g_spi1.cfg2 = 0x00000000;
    g_spi1.ier = 0x00000000;
    g_spi1.cgfr = 0x00000000;

    return &g_spi1;
}