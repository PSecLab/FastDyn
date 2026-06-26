#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/spi.h>

#define SPI2_BASE_ADDR 0x40003800ULL

#define SPI_CR1_OFF     0x00
#define SPI_CR2_OFF     0x04
#define SPI_SR_OFF      0x08
#define SPI_DR_OFF      0x0C
#define SPI_CRCPR_OFF   0x10
#define SPI_RXCRCR_OFF  0x14
#define SPI_TXCRCR_OFF  0x18
#define SPI_I2SCFGR_OFF 0x1C
#define SPI_I2SPR_OFF   0x20

#define SPI_CR1_SPE        (1U << 6)
#define SPI_CR2_RXDMAEN    (1U << 0)
#define SPI_CR2_TXDMAEN    (1U << 1)

#define SPI_SR_RXNE     (1U << 0)
#define SPI_SR_TXE      (1U << 1)
#define SPI_SR_BSY      (1U << 7)

#define SPI2_CS_PD10_SIGNAL_ID 58
#define DMA1_STREAM_SPI2_RX    3
#define DMA1_STREAM_SPI2_TX    4

#define RAMTRON_CMD_WREN       0x06
#define RAMTRON_CMD_WRITE      0x02
#define RAMTRON_CMD_READ       0x03
#define RAMTRON_CMD_RDSR       0x05
#define RAMTRON_CMD_RDID       0x9F

typedef struct {
    SPIBus bus;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t sr;
    uint32_t dr;
    uint32_t crcpr;
    uint32_t rxcrcr;
    uint32_t txcrcr;
    uint32_t i2scfgr;
    uint32_t i2spr;
    int active_cs_id;
    bool cs_asserted;
    bool fallback_cs_active;
    bool fallback_force_continuation;
} SPI2State;

static SPI2State g_spi2_state;

static uint32_t spi2_mask_for_size(unsigned size) {
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    default:
        return 0xFFFFFFFFU;
    }
}

static uint32_t spi2_extract_reg32(uint32_t reg, uint64_t suboff, unsigned size) {
    uint32_t mask = spi2_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);

    if (size >= 4) {
        return reg;
    }
    return (reg >> shift) & mask;
}

static uint32_t spi2_merge_reg32(uint32_t oldv, uint64_t suboff, uint64_t value, unsigned size) {
    uint32_t mask = spi2_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);
    uint32_t fullmask = (size >= 4) ? 0xFFFFFFFFU : (mask << shift);

    if (size >= 4) {
        return (uint32_t)value;
    }
    return (oldv & ~fullmask) | ((((uint32_t)value) & mask) << shift);
}

static bool spi2_is_ramtron_opcode(uint8_t byte) {
    switch (byte) {
    case RAMTRON_CMD_WREN:
    case RAMTRON_CMD_WRITE:
    case RAMTRON_CMD_READ:
    case RAMTRON_CMD_RDSR:
    case RAMTRON_CMD_RDID:
        return true;
    default:
        return false;
    }
}

static bool spi2_fallback_chunk_needs_continuation(uint8_t first_byte, int len) {
    switch (first_byte) {
    case RAMTRON_CMD_WRITE:
    case RAMTRON_CMD_READ:
        return len <= 4;
    case RAMTRON_CMD_RDSR:
    case RAMTRON_CMD_RDID:
        return true;
    default:
        return false;
    }
}

static void spi2_fallback_prepare_chunk(SPI2State *s, uint8_t first_byte, int len) {
    if (s == NULL || len <= 0) {
        return;
    }
    if (s->active_cs_id < 0 || s->bus.Slaves.num_slaves != 1) {
        return;
    }
    if (s->cs_asserted) {
        s->fallback_force_continuation = false;
        return;
    }

    /*
     * If GPIO-driven CS wasn't observed, synthesize a command boundary only at
     * DMA/PIO chunk boundaries. This keeps split command+payload exchanges
     * contiguous while preventing opcode-like bytes inside a payload from being
     * misinterpreted as a fresh command.
     */
    if (s->fallback_cs_active &&
        !s->fallback_force_continuation &&
        spi2_is_ramtron_opcode(first_byte)) {
        api_spi_set_cs(&s->bus, s->active_cs_id, 1);
        api_spi_set_cs(&s->bus, s->active_cs_id, 0);
        s->fallback_cs_active = true;
    }
}

static void spi2_fallback_complete_chunk(SPI2State *s, uint8_t first_byte, int len) {
    if (s == NULL) {
        return;
    }
    if (s->active_cs_id < 0 || s->bus.Slaves.num_slaves != 1) {
        s->fallback_force_continuation = false;
        return;
    }
    if (s->cs_asserted || !s->fallback_cs_active) {
        s->fallback_force_continuation = false;
        return;
    }

    s->fallback_force_continuation =
        spi2_fallback_chunk_needs_continuation(first_byte, len);
}

static void spi2_maybe_request_tx_dma(SPI2State *s) {
    if (s == NULL) {
        return;
    }
    if ((s->cr1 & SPI_CR1_SPE) == 0) {
        return;
    }
    if ((s->cr2 & SPI_CR2_TXDMAEN) == 0) {
        return;
    }

    api_dma_request(1, DMA1_STREAM_SPI2_TX);
}

static void spi2_set_all_cs_inactive(SPI2State *s) {
    int i;

    for (i = 0; i < s->bus.Slaves.num_slaves; i++) {
        api_spi_set_cs(&s->bus, s->bus.Slaves.slave[i].cs_id, 1);
    }
    s->cs_asserted = false;
    s->fallback_cs_active = false;
    s->fallback_force_continuation = false;
}

static void spi2_pd10_signal(void *opaque, int signal_id, bool level) {
    SPI2State *s = (SPI2State *)opaque;

    if (s == NULL || signal_id != SPI2_CS_PD10_SIGNAL_ID) {
        return;
    }
    if (s->active_cs_id < 0) {
        return;
    }

    api_spi_set_cs(&s->bus, s->active_cs_id, level ? 1 : 0);
    s->cs_asserted = !level;

    if (!level) {
        s->fallback_cs_active = false;
        s->fallback_force_continuation = false;
        spi2_maybe_request_tx_dma(s);
    } else {
        s->fallback_cs_active = false;
        s->fallback_force_continuation = false;
    }
}

static bool spi2_transfer_begin(SPI2State *s, bool *used_fallback_cs) {
    if (used_fallback_cs != NULL) {
        *used_fallback_cs = false;
    }
    if (s == NULL) {
        return false;
    }
    if ((s->cr1 & SPI_CR1_SPE) == 0 || s->active_cs_id < 0) {
        return false;
    }
    if (s->cs_asserted || s->fallback_cs_active) {
        return true;
    }

    /*
     * The board only wires one SPI2 slave. If the GPIO->CS signal was not
     * observed, keep that sole slave selected across the whole transaction
     * instead of deselecting between DMA chunks. AP_RAMTRON RDID is commonly
     * split into 8-byte and 2-byte bursts and requires CS continuity.
     */
    if (s->bus.Slaves.num_slaves == 1) {
        api_spi_set_cs(&s->bus, s->active_cs_id, 0);
        s->fallback_cs_active = true;
        if (used_fallback_cs != NULL) {
            *used_fallback_cs = true;
        }
        return true;
    }

    return false;
}

static void spi2_transfer_end(SPI2State *s, bool used_fallback_cs) {
    (void)s;
    (void)used_fallback_cs;
}

static void spi2_dma_tx_data(void *opaque, const uint8_t *data, int len) {
    SPI2State *s = (SPI2State *)opaque;
    uint8_t rxbuf[8];
    bool can_transfer;
    bool used_fallback_cs = false;
    int i;

    if (s == NULL) {
        s = &g_spi2_state;
    }
    if (data == NULL || len <= 0) {
        return;
    }
    if (len > (int)sizeof(rxbuf)) {
        len = (int)sizeof(rxbuf);
    }

    spi2_fallback_prepare_chunk(s, data[0], len);
    can_transfer = spi2_transfer_begin(s, &used_fallback_cs);

    s->sr |= SPI_SR_BSY;
    s->sr &= ~SPI_SR_RXNE;

    for (i = 0; i < len; i++) {
        uint8_t txb = data[i];
        uint8_t rxb = 0xFF;

        if (can_transfer) {
            rxb = (uint8_t)(api_spi_transfer(&s->bus, txb) & 0xFFU);
        }

        s->dr = rxb;
        s->txcrcr = txb;
        s->rxcrcr = rxb;
        rxbuf[i] = rxb;
    }

    spi2_transfer_end(s, used_fallback_cs);
    if (can_transfer) {
        spi2_fallback_complete_chunk(s, data[0], len);
    }

    s->sr |= SPI_SR_TXE;
    s->sr &= ~SPI_SR_BSY;

    if (len > 0) {
        s->sr |= SPI_SR_RXNE;
    }

    if ((s->cr2 & SPI_CR2_RXDMAEN) != 0) {
        if (api_dma_request_data(1, DMA1_STREAM_SPI2_RX, 0x4000380C, rxbuf, len) >= 0) {
            s->sr &= ~SPI_SR_RXNE;
        } else {
            dev_debug("spi2: failed to submit RX DMA payload");
        }
    }
}

void* spi2_init(ConfigSection* model_info) {
    memset(&g_spi2_state, 0, sizeof(g_spi2_state));

    g_spi2_state.bus = api_spi_init_bus(model_info, "spi2");
    g_spi2_state.sr = SPI_SR_TXE;
    g_spi2_state.active_cs_id = -1;

    if (g_spi2_state.bus.Slaves.num_slaves > 0) {
        g_spi2_state.active_cs_id = g_spi2_state.bus.Slaves.slave[0].cs_id;
    } else {
        dev_debug("spi2: initialized without attached SPI slaves");
    }

    spi2_set_all_cs_inactive(&g_spi2_state);
    api_signal_register(SPI2_CS_PD10_SIGNAL_ID, spi2_pd10_signal, &g_spi2_state);
    api_dma_register_stream_data(1, DMA1_STREAM_SPI2_TX, spi2_dma_tx_data, &g_spi2_state);

    return &g_spi2_state;
}

uint64_t spi2_read(void *opaque, uint64_t addr, unsigned size) {
    SPI2State *s = (SPI2State *)opaque;
    uint64_t offset = addr - SPI2_BASE_ADDR;

    if (s == NULL) {
        s = &g_spi2_state;
    }

    switch (offset & ~0x3ULL) {
    case SPI_CR1_OFF:
        return spi2_extract_reg32(s->cr1, offset - SPI_CR1_OFF, size);
    case SPI_CR2_OFF:
        return spi2_extract_reg32(s->cr2, offset - SPI_CR2_OFF, size);
    case SPI_SR_OFF:
        return spi2_extract_reg32(s->sr | SPI_SR_TXE, offset - SPI_SR_OFF, size);
    case SPI_DR_OFF: {
        uint32_t ret = spi2_extract_reg32(s->dr, offset - SPI_DR_OFF, size);
        s->sr &= ~SPI_SR_RXNE;
        return ret;
    }
    case SPI_CRCPR_OFF:
        return spi2_extract_reg32(s->crcpr, offset - SPI_CRCPR_OFF, size);
    case SPI_RXCRCR_OFF:
        return spi2_extract_reg32(s->rxcrcr, offset - SPI_RXCRCR_OFF, size);
    case SPI_TXCRCR_OFF:
        return spi2_extract_reg32(s->txcrcr, offset - SPI_TXCRCR_OFF, size);
    case SPI_I2SCFGR_OFF:
        return spi2_extract_reg32(s->i2scfgr, offset - SPI_I2SCFGR_OFF, size);
    case SPI_I2SPR_OFF:
        return spi2_extract_reg32(s->i2spr, offset - SPI_I2SPR_OFF, size);
    default:
        return 0;
    }
}

void spi2_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    SPI2State *s = (SPI2State *)opaque;
    uint64_t offset = addr - SPI2_BASE_ADDR;

    if (s == NULL) {
        s = &g_spi2_state;
    }

    switch (offset & ~0x3ULL) {
    case SPI_CR1_OFF:
        s->cr1 = spi2_merge_reg32(s->cr1, offset - SPI_CR1_OFF, value, size);
        if ((s->cr1 & SPI_CR1_SPE) == 0) {
            spi2_set_all_cs_inactive(s);
        } else {
            spi2_maybe_request_tx_dma(s);
        }
        break;

    case SPI_CR2_OFF:
        s->cr2 = spi2_merge_reg32(s->cr2, offset - SPI_CR2_OFF, value, size);
        spi2_maybe_request_tx_dma(s);
        break;

    case SPI_DR_OFF: {
        uint32_t rx = 0xFF;
        unsigned i;
        unsigned bytes = size;
        bool can_transfer;
        bool used_fallback_cs = false;

        if (bytes == 0) {
            bytes = 1;
        }
        if (bytes > 4) {
            bytes = 4;
        }

        spi2_fallback_prepare_chunk(s, (uint8_t)(value & 0xFFU), (int)bytes);
        can_transfer = spi2_transfer_begin(s, &used_fallback_cs);

        s->sr |= SPI_SR_BSY;
        s->sr &= ~SPI_SR_RXNE;

        for (i = 0; i < bytes; i++) {
            uint8_t txb = (uint8_t)((value >> (i * 8)) & 0xFFU);

            if (can_transfer) {
                rx = api_spi_transfer(&s->bus, txb) & 0xFFU;
            } else {
                rx = 0xFF;
            }

            s->txcrcr = txb;
            s->rxcrcr = rx;
            s->dr = rx;
        }

        spi2_transfer_end(s, used_fallback_cs);
        if (can_transfer) {
            spi2_fallback_complete_chunk(s, (uint8_t)(value & 0xFFU), (int)bytes);
        }

        s->sr |= SPI_SR_RXNE | SPI_SR_TXE;
        s->sr &= ~SPI_SR_BSY;
        break;
    }

    case SPI_CRCPR_OFF:
        s->crcpr = spi2_merge_reg32(s->crcpr, offset - SPI_CRCPR_OFF, value, size);
        break;

    case SPI_I2SCFGR_OFF:
        s->i2scfgr = spi2_merge_reg32(s->i2scfgr, offset - SPI_I2SCFGR_OFF, value, size);
        break;

    case SPI_I2SPR_OFF:
        s->i2spr = spi2_merge_reg32(s->i2spr, offset - SPI_I2SPR_OFF, value, size);
        break;

    default:
        break;
    }
}
