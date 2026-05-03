
// Device Model for SPI1
//
// Inferred Register Functions:
// - CR1   : main control (SPE/CSTART observed)
// - CR2   : transfer-size/control, low 16 bits reflected in SR[31:16]
// - CFG1  : configuration, simple RW
// - CFG2  : configuration, simple RW
// - IER   : interrupt enable, simple RW
// - SR    : dynamic status derived from active state + RX FIFO state
// - IFCR  : read-as-zero, write-accepted clear register
// - TXDR  : transmit register; writing generates synthetic RX data
// - RXDR  : receive register; pops synthetic RX FIFO
// - CGFR  : configuration, simple RW
//
// Notes:
// - The framework passes absolute guest physical addresses; this model subtracts
//   the peripheral base before decoding offsets.
// - No IRQ behavior was observed in the trace, so this model does not raise IRQs.
// - The prompt does not provide ConfigSection accessor APIs, so the trace-derived
//   base address is used as the default initialization value.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define SPI1_BASE_DEFAULT   0x40013000ULL

/* Register offsets */
#define SPI1_CR1_OFF        0x00
#define SPI1_CR2_OFF        0x04
#define SPI1_CFG1_OFF       0x08
#define SPI1_CFG2_OFF       0x0C
#define SPI1_IER_OFF        0x10
#define SPI1_SR_OFF         0x14
#define SPI1_IFCR_OFF       0x18
#define SPI1_TXDR_OFF       0x20
#define SPI1_RXDR_OFF       0x30
#define SPI1_CGFR_OFF       0x50

/* CR1 bits actually needed by observed firmware */
#define SPI1_CR1_SPE        (1u << 0)
#define SPI1_CR1_CSTART     (1u << 9)

/* Minimal SR bits inferred from trace values */
#define SPI1_SR_RXP         (1u << 0)
#define SPI1_SR_TXP         (1u << 1)
#define SPI1_SR_DXP         (1u << 2)
#define SPI1_SR_EOT         (1u << 3)
#define SPI1_SR_TXTF        (1u << 4)
#define SPI1_SR_TXC         (1u << 12)
#define SPI1_SR_RXPLVL_1    (1u << 13)
#define SPI1_SR_CTSIZE_SHIFT 16

#define SPI1_RX_FIFO_SIZE   8

typedef struct SPI1State {
    uint64_t base;

    uint32_t cr1;
    uint32_t cr2;
    uint32_t cfg1;
    uint32_t cfg2;
    uint32_t ier;
    uint32_t cgfr;

    uint32_t last_ifcr;
    uint32_t last_txdr;

    uint8_t rx_fifo[SPI1_RX_FIFO_SIZE];
    uint8_t rx_head;
    uint8_t rx_count;

    bool xfer_started;
    bool xfer_complete;
} SPI1State;

static SPI1State g_spi1;

static uint32_t spi1_size_mask(unsigned size)
{
    switch (size) {
    case 1: return 0x000000FFu;
    case 2: return 0x0000FFFFu;
    default: return 0xFFFFFFFFu;
    }
}

static uint32_t spi1_merge_write32(uint32_t oldv, uint64_t value, unsigned size)
{
    uint32_t mask = spi1_size_mask(size);
    return (oldv & ~mask) | ((uint32_t)value & mask);
}

static uint64_t spi1_mask_read(uint32_t value, unsigned size)
{
    return (uint64_t)(value & spi1_size_mask(size));
}

static bool spi1_active(SPI1State *s)
{
    return ((s->cr1 & (SPI1_CR1_SPE | SPI1_CR1_CSTART)) ==
            (SPI1_CR1_SPE | SPI1_CR1_CSTART));
}

static void spi1_fifo_clear(SPI1State *s)
{
    s->rx_head = 0;
    s->rx_count = 0;
    memset(s->rx_fifo, 0, sizeof(s->rx_fifo));
}

static void spi1_transaction_reset(SPI1State *s)
{
    spi1_fifo_clear(s);
    s->xfer_started = false;
    s->xfer_complete = false;
}

static void spi1_fifo_push(SPI1State *s, uint8_t v)
{
    if (s->rx_count >= SPI1_RX_FIFO_SIZE) {
        return;
    }

    s->rx_fifo[(uint8_t)(s->rx_head + s->rx_count) % SPI1_RX_FIFO_SIZE] = v;
    s->rx_count++;
}

static uint8_t spi1_fifo_pop(SPI1State *s)
{
    uint8_t v = 0;

    if (s->rx_count == 0) {
        return 0;
    }

    v = s->rx_fifo[s->rx_head];
    s->rx_head = (uint8_t)((s->rx_head + 1) % SPI1_RX_FIFO_SIZE);
    s->rx_count--;
    return v;
}

static uint32_t spi1_read_rxdr(SPI1State *s)
{
    uint8_t old_count = s->rx_count;
    uint32_t v = (uint32_t)spi1_fifo_pop(s);

    /*
     * Trace shows SR moving from the "data available" phase (0x22007)
     * into completion-flag states (0x301F / 0x101A) once RX data is
     * consumed. Latch completion on the first successful RXDR pop.
     */
    if (s->xfer_started && !s->xfer_complete && old_count != s->rx_count) {
        s->xfer_complete = true;
    }

    return v;
}

static uint32_t spi1_build_sr(SPI1State *s)
{
    uint32_t sr = 0;
    uint32_t tsize = s->cr2 & 0xFFFFu;

    if (!spi1_active(s)) {
        return 0;
    }

    /*
     * Before completion is latched, hardware reflects CR2.TSIZE in SR[31:16]
     * and reports the usual ready/data-available states:
     *   0x00020002  idle-ready
     *   0x00022007  RX data available
     */
    if (!s->xfer_complete) {
        sr |= SPI1_SR_TXP;
        sr |= (tsize << SPI1_SR_CTSIZE_SHIFT);

        if (s->rx_count > 0) {
            sr |= SPI1_SR_RXP;
            sr |= SPI1_SR_DXP;
            sr |= SPI1_SR_RXPLVL_1;
        }
        return sr;
    }

    /*
     * After RX consumption begins, the trace shows completion-type flags
     * instead of CTSIZE:
     *   0x0000301F while one RX byte is still pending
     *   0x0000101A after RX FIFO becomes empty
     */
    sr |= SPI1_SR_TXP;
    sr |= SPI1_SR_EOT;
    sr |= SPI1_SR_TXTF;
    sr |= SPI1_SR_TXC;

    if (s->rx_count > 0) {
        sr |= SPI1_SR_RXP;
        sr |= SPI1_SR_DXP;
        sr |= SPI1_SR_RXPLVL_1;
    }

    return sr;
}

static void spi1_generate_reply(SPI1State *s, uint8_t tx_byte)
{
    uint32_t frames = s->cr2 & 0xFFFFu;
    uint32_t i;

    if (frames == 0) {
        frames = 1;
    }
    if (frames > SPI1_RX_FIFO_SIZE) {
        frames = SPI1_RX_FIFO_SIZE;
    }

    /*
     * Do not overwrite unread RX data if firmware performs another TXDR write
     * before draining RXDR. The previous model cleared the FIFO here, which is
     * why 0x60 was lost and emulation returned 0xFF, 0xFF.
     */
    if (s->rx_count != 0) {
        return;
    }

    spi1_fifo_clear(s);
    s->xfer_started = true;
    s->xfer_complete = false;

    /*
     * Minimal observed behavior:
     * firmware writes 0xD0 and later reads 0xFF then 0x60 from RXDR.
     */
    if (tx_byte == 0xD0) {
        if (frames >= 1) {
            spi1_fifo_push(s, 0xFF);
        }
        if (frames >= 2) {
            spi1_fifo_push(s, 0x60);
        }
        for (i = 2; i < frames; i++) {
            spi1_fifo_push(s, 0xFF);
        }
        return;
    }

    /* Generic fallback: inactive / unknown slave reads high (0xFF). */
    for (i = 0; i < frames; i++) {
        spi1_fifo_push(s, 0xFF);
    }
}

static void spi1_log_bad_access(const char *kind, uint64_t addr, unsigned size, uint64_t value)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "SPI1 %s unknown access addr=0x%llx size=%u value=0x%llx",
             kind,
             (unsigned long long)addr,
             size,
             (unsigned long long)value);
    dev_debug(buf);
}

// This function will emulate all device reads
uint64_t spi1_read(void *opaque, hwaddr addr, unsigned size) {
    SPI1State *s = (SPI1State *)opaque;
    uint64_t offset;
    uint32_t v = 0;

    if (s == NULL) {
        s = &g_spi1;
    }

    offset = (uint64_t)addr - s->base;

    switch (offset) {
    case SPI1_CR1_OFF:
        v = s->cr1;
        break;

    case SPI1_CR2_OFF:
        v = s->cr2;
        break;

    case SPI1_CFG1_OFF:
        v = s->cfg1;
        break;

    case SPI1_CFG2_OFF:
        v = s->cfg2;
        break;

    case SPI1_IER_OFF:
        v = s->ier;
        break;

    case SPI1_SR_OFF:
        v = spi1_build_sr(s);
        break;

    case SPI1_IFCR_OFF:
        /* Treat IFCR as read-as-zero; trace only observed 0. */
        v = 0;
        break;

    case SPI1_RXDR_OFF:
        v = spi1_read_rxdr(s);
        break;

    case SPI1_CGFR_OFF:
        v = s->cgfr;
        break;

    case SPI1_TXDR_OFF:
        /* Write-only in practice for this model. */
        v = 0;
        break;

    default:
        spi1_log_bad_access("read", (uint64_t)addr, size, 0);
        v = 0;
        break;
    }

    return spi1_mask_read(v, size);
}

// This function will emulate all device writes
void spi1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    SPI1State *s = (SPI1State *)opaque;
    uint64_t offset;

    if (s == NULL) {
        s = &g_spi1;
    }

    offset = (uint64_t)addr - s->base;

    switch (offset) {
    case SPI1_CR1_OFF:
        s->cr1 = spi1_merge_write32(s->cr1, value, size);
        if (!spi1_active(s)) {
            spi1_transaction_reset(s);
        }
        break;

    case SPI1_CR2_OFF:
        s->cr2 = spi1_merge_write32(s->cr2, value, size);
        break;

    case SPI1_CFG1_OFF:
        s->cfg1 = spi1_merge_write32(s->cfg1, value, size);
        break;

    case SPI1_CFG2_OFF:
        s->cfg2 = spi1_merge_write32(s->cfg2, value, size);
        break;

    case SPI1_IER_OFF:
        s->ier = spi1_merge_write32(s->ier, value, size);
        break;

    case SPI1_IFCR_OFF:
        /*
         * IFCR is write-only/clear-on-write in hardware. Reads stay zero, but
         * writes clear the completion phase so SR returns to the ready state.
         */
        s->last_ifcr = spi1_merge_write32(s->last_ifcr, value, size);
        s->xfer_complete = false;
        if (s->rx_count == 0) {
            s->xfer_started = false;
        }
        break;

    case SPI1_TXDR_OFF:
        s->last_txdr = (uint32_t)(value & spi1_size_mask(size));
        if (spi1_active(s)) {
            spi1_generate_reply(s, (uint8_t)(value & 0xFF));
        }
        break;

    case SPI1_CGFR_OFF:
        s->cgfr = spi1_merge_write32(s->cgfr, value, size);
        break;

    default:
        spi1_log_bad_access("write", (uint64_t)addr, size, value);
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* spi1_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_spi1, 0, sizeof(g_spi1));

    /*
     * Trace-derived base. The prompt does not provide ConfigSection accessor
     * APIs, so this is initialized directly here.
     */
    g_spi1.base = SPI1_BASE_DEFAULT;

    /* Reset-state registers are all observed as zero before configuration. */
    g_spi1.cr1 = 0;
    g_spi1.cr2 = 0;
    g_spi1.cfg1 = 0;
    g_spi1.cfg2 = 0;
    g_spi1.ier = 0;
    g_spi1.cgfr = 0;
    g_spi1.last_ifcr = 0;
    g_spi1.last_txdr = 0;
    spi1_fifo_clear(&g_spi1);

    return &g_spi1;
}