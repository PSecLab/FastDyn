// Device Model for SPI1

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define SPI1_BASE 0x40013000ULL

// Register offsets
#define SPI1_CR1_OFF    0x00
#define SPI1_CR2_OFF    0x04
#define SPI1_CFG1_OFF   0x08
#define SPI1_CFG2_OFF   0x0C
#define SPI1_IER_OFF    0x10
#define SPI1_SR_OFF     0x14
#define SPI1_IFCR_OFF   0x18
#define SPI1_TXDR_OFF   0x20
#define SPI1_RXDR_OFF   0x30
#define SPI1_CGFR_OFF   0x50

// Inferred CR1 bits
#define SPI1_CR1_SPE        (1u << 0)
#define SPI1_CR1_CSTART     (1u << 9)

// Minimal but stateful SR/IFCR bit definitions for STM32-style SPI v2 behavior.
#define SPI1_SR_RXP         (1u << 0)
#define SPI1_SR_TXP         (1u << 1)
#define SPI1_SR_DXP         (1u << 2)
#define SPI1_SR_EOT         (1u << 3)
#define SPI1_SR_TXTF        (1u << 4)
#define SPI1_SR_TXC         (1u << 12)
#define SPI1_SR_RXPLVL      (1u << 13)

#define SPI1_IFCR_EOTC      (1u << 3)
#define SPI1_IFCR_TXTFC     (1u << 4)

// CR2 low 16 bits are treated as TSIZE source; SR[31:16] exposes CTSIZE.
#define SPI1_CR2_TSIZE_MASK 0x0000FFFFu
#define SPI1_RX_FIFO_DEPTH  8u

typedef struct {
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cfg1;
    uint32_t cfg2;
    uint32_t ier;
    uint32_t ifcr;
    uint32_t cgfr;

    uint32_t txdr;
    uint32_t rxdr;

    uint16_t ctsize;
    uint16_t frames_remaining;
    bool transfer_active;
    bool rx_ready;
    bool eot;
    bool txtf;
    bool txc;

    uint32_t rx_fifo[SPI1_RX_FIFO_DEPTH];
    uint8_t rx_fifo_head;
    uint8_t rx_fifo_tail;
    uint8_t rx_fifo_count;

    SPIBus bus;
    int active_cs;
    bool cs_asserted;
} SPI1State;

static SPI1State spi1_state;

static uint32_t spi1_mask_for_size(unsigned size) {
    switch (size) {
    case 1:
        return 0xFFu;
    case 2:
        return 0xFFFFu;
    default:
        return 0xFFFFFFFFu;
    }
}

static uint32_t spi1_merge_write32(uint32_t oldv, uint64_t value, unsigned size) {
    uint32_t mask = spi1_mask_for_size(size);
    return (oldv & ~mask) | ((uint32_t)value & mask);
}

static void spi1_debug_access(const char *kind, hwaddr addr, uint64_t value) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "SPI1 %s unknown addr=0x%08llx value=0x%08llx",
             kind,
             (unsigned long long)addr,
             (unsigned long long)value);
    dev_debug(buf);
}

static unsigned spi1_frame_bits(SPI1State *s) {
    // Minimal interpretation based on STM32H7 SPI CFG1 low data-size field.
    // If CFG1[4:0] == 7, that corresponds to 8-bit frames.
    unsigned bits = (s->cfg1 & 0x1Fu) + 1u;
    if (bits == 0 || bits > 32) {
        bits = 8;
    }
    return bits;
}

static uint32_t spi1_frame_mask(SPI1State *s) {
    unsigned bits = spi1_frame_bits(s);
    if (bits >= 32) {
        return 0xFFFFFFFFu;
    }
    return (1u << bits) - 1u;
}

static void spi1_rx_fifo_reset(SPI1State *s) {
    s->rx_fifo_head = 0;
    s->rx_fifo_tail = 0;
    s->rx_fifo_count = 0;
    s->rx_ready = false;
}

static void spi1_rx_fifo_push(SPI1State *s, uint32_t value) {
    if (s->rx_fifo_count >= SPI1_RX_FIFO_DEPTH) {
        s->rx_fifo_head = (uint8_t)((s->rx_fifo_head + 1u) % SPI1_RX_FIFO_DEPTH);
        s->rx_fifo_count--;
    }

    s->rx_fifo[s->rx_fifo_tail] = value;
    s->rx_fifo_tail = (uint8_t)((s->rx_fifo_tail + 1u) % SPI1_RX_FIFO_DEPTH);
    s->rx_fifo_count++;
    s->rx_ready = true;
}

static bool spi1_rx_fifo_pop(SPI1State *s, uint32_t *value) {
    if (s->rx_fifo_count == 0) {
        s->rx_ready = false;
        return false;
    }

    *value = s->rx_fifo[s->rx_fifo_head];
    s->rx_fifo_head = (uint8_t)((s->rx_fifo_head + 1u) % SPI1_RX_FIFO_DEPTH);
    s->rx_fifo_count--;
    s->rx_ready = (s->rx_fifo_count != 0);
    return true;
}

static void spi1_assert_cs(SPI1State *s) {
    if (s->bus.Slaves.num_slaves <= 0) {
        s->cs_asserted = false;
        return;
    }

    if (!s->cs_asserted) {
        api_spi_set_cs(&s->bus, s->active_cs, 0);
        s->cs_asserted = true;
    }
}

static void spi1_deassert_cs(SPI1State *s) {
    if (s->bus.Slaves.num_slaves <= 0) {
        s->cs_asserted = false;
        return;
    }

    if (s->cs_asserted) {
        api_spi_set_cs(&s->bus, s->active_cs, 1);
        s->cs_asserted = false;
    }
}

static void spi1_start_transfer(SPI1State *s) {
    s->ctsize = (uint16_t)(s->cr2 & SPI1_CR2_TSIZE_MASK);
    s->frames_remaining = s->ctsize;
    s->transfer_active = true;
    s->eot = false;
    s->txtf = false;
    s->txc = false;
    spi1_rx_fifo_reset(s);
    spi1_assert_cs(s);
}

static void spi1_complete_transfer(SPI1State *s) {
    s->transfer_active = false;
    s->ctsize = 0;
    s->frames_remaining = 0;
    s->eot = true;
    s->txtf = true;
    s->txc = true;
    spi1_deassert_cs(s);
}

static uint32_t spi1_build_sr(SPI1State *s) {
    uint32_t sr = 0;
    bool enabled = (s->cr1 & SPI1_CR1_SPE) != 0;
    bool txp = false;

    /*
     * On STM32-style SPI v2, CSTART is a start command rather than a persistent
     * "transfer is running" bit. Firmware commonly polls TXP once SPI is enabled,
     * so do not gate TXP on the software-visible CR1.CSTART value.
     */
    if (enabled) {
        if (!s->transfer_active) {
            txp = true;
        } else if ((s->cr2 & SPI1_CR2_TSIZE_MASK) == 0 || s->frames_remaining > 0) {
            txp = true;
        }
    }

    if (s->rx_fifo_count > 0) {
        sr |= SPI1_SR_RXP | SPI1_SR_RXPLVL;
    }

    if (txp) {
        sr |= SPI1_SR_TXP;
    }

    if (txp && s->rx_fifo_count > 0) {
        sr |= SPI1_SR_DXP;
    }

    if (s->eot) {
        sr |= SPI1_SR_EOT;
    }
    if (s->txtf) {
        sr |= SPI1_SR_TXTF;
    }
    if (s->txc) {
        sr |= SPI1_SR_TXC;
    }

    if (s->transfer_active) {
        sr |= ((uint32_t)s->ctsize << 16);
    }

    return sr;
}

static void spi1_do_transfer(SPI1State *s, uint32_t value) {
    uint32_t rx = 0;
    uint32_t mask = spi1_frame_mask(s);

    s->txdr = value & mask;

    if ((s->cr1 & SPI1_CR1_SPE) == 0) {
        return;
    }

    /*
     * Transfers are command-driven: TXDR writes only clock data when a transfer
     * has already been started by a prior CSTART command.
     */
    if (!s->transfer_active) {
        return;
    }

    if (s->bus.Slaves.num_slaves > 0) {
        rx = api_spi_transfer(&s->bus, s->txdr);
    } else {
        // No attached slave: return 0 on MISO.
        rx = 0;
    }

    s->rxdr = rx & mask;
    spi1_rx_fifo_push(s, s->rxdr);

    if (s->frames_remaining > 0) {
        s->frames_remaining--;
        if (s->frames_remaining == 0) {
            spi1_complete_transfer(s);
        }
    }
}

// This function will emulation all device reads
uint64_t spi1_read(void *opaque, hwaddr addr, unsigned size) {
    SPI1State *s = (SPI1State *)opaque;
    hwaddr offset = addr - SPI1_BASE;
    uint32_t val = 0;

    switch (offset) {
    case SPI1_CR1_OFF:
        val = s->cr1;
        break;
    case SPI1_CR2_OFF:
        val = s->cr2;
        break;
    case SPI1_CFG1_OFF:
        val = s->cfg1;
        break;
    case SPI1_CFG2_OFF:
        val = s->cfg2;
        break;
    case SPI1_IER_OFF:
        val = s->ier;
        break;
    case SPI1_SR_OFF:
        val = spi1_build_sr(s);
        break;
    case SPI1_IFCR_OFF:
        val = 0;
        break;
    case SPI1_TXDR_OFF:
        val = s->txdr;
        break;
    case SPI1_RXDR_OFF: {
        uint32_t rx;
        if (spi1_rx_fifo_pop(s, &rx)) {
            s->rxdr = rx;
            val = rx;
        } else {
            val = s->rxdr;
        }
        break;
    }
    case SPI1_CGFR_OFF:
        val = s->cgfr;
        break;
    default:
        spi1_debug_access("read", addr, 0);
        val = 0;
        break;
    }

    return (uint64_t)(val & spi1_mask_for_size(size));
}

// This function will emulate all device writes
void spi1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    SPI1State *s = (SPI1State *)opaque;
    hwaddr offset = addr - SPI1_BASE;
    uint32_t old_cr1;

    switch (offset) {
    case SPI1_CR1_OFF: {
        uint32_t written = (uint32_t)value & spi1_mask_for_size(size);
        bool cstart_cmd;

        old_cr1 = s->cr1;
        s->cr1 = spi1_merge_write32(s->cr1, value, size);
        cstart_cmd = (written & SPI1_CR1_CSTART) != 0;

        /*
         * CSTART is a transient start command on STM32 SPI v2. Do not leave it
         * latched in the readable CR1 state or use its stored value as a proxy
         * for "transfer still active".
         */
        s->cr1 &= ~SPI1_CR1_CSTART;

        if ((s->cr1 & SPI1_CR1_SPE) == 0) {
            s->transfer_active = false;
            s->ctsize = 0;
            s->frames_remaining = 0;
            spi1_rx_fifo_reset(s);
            s->eot = false;
            s->txtf = false;
            s->txc = false;
            spi1_deassert_cs(s);
            break;
        }

        if (((old_cr1 & SPI1_CR1_SPE) == 0) && !cstart_cmd) {
            s->eot = false;
            s->txtf = false;
            s->txc = false;
        }

        if (cstart_cmd && !s->transfer_active) {
            spi1_start_transfer(s);
        }
        break;
    }

    case SPI1_CR2_OFF:
        s->cr2 = spi1_merge_write32(s->cr2, value, size);
        if (!s->transfer_active) {
            s->ctsize = 0;
            s->frames_remaining = 0;
            s->eot = false;
            s->txtf = false;
            s->txc = false;
        }
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

    case SPI1_IFCR_OFF: {
        uint32_t clear_mask = (uint32_t)value & spi1_mask_for_size(size);
        s->ifcr = 0;

        if (clear_mask & SPI1_IFCR_EOTC) {
            s->eot = false;
        }
        if (clear_mask & SPI1_IFCR_TXTFC) {
            s->txtf = false;
            s->txc = false;
        }
        break;
    }

    case SPI1_TXDR_OFF:
        spi1_do_transfer(s, (uint32_t)value);
        break;

    case SPI1_RXDR_OFF:
        // RXDR is read-oriented in this model; ignore writes.
        break;

    case SPI1_CGFR_OFF:
        s->cgfr = spi1_merge_write32(s->cgfr, value, size);
        break;

    default:
        spi1_debug_access("write", addr, value);
        break;
    }
}

void* spi1_init(ConfigSection* model_info) {
    int i;

    memset(&spi1_state, 0, sizeof(spi1_state));

    // Initialize external SPI bus from platform configuration.
    spi1_state.bus = api_spi_init_bus(model_info);

    // Use first configured slave's CS if available, otherwise default to 0.
    spi1_state.active_cs = 0;
    if (spi1_state.bus.Slaves.num_slaves > 0) {
        spi1_state.active_cs = spi1_state.bus.Slaves.slave[0].cs_id;
    }

    // Ensure all configured chip selects start inactive.
    for (i = 0; i < spi1_state.bus.Slaves.num_slaves; i++) {
        api_spi_set_cs(&spi1_state.bus, spi1_state.bus.Slaves.slave[i].cs_id, 1);
    }

    spi1_state.cs_asserted = false;
    spi1_state.ctsize = 0;
    spi1_state.frames_remaining = 0;
    spi1_state.transfer_active = false;
    spi1_rx_fifo_reset(&spi1_state);
    spi1_state.eot = false;
    spi1_state.txtf = false;
    spi1_state.txc = false;

    return &spi1_state;
}