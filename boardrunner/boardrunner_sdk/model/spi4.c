#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/dma.h>
#include <boardrunner/signals.h>
#include <boardrunner/vio.h>
#include <boardrunner/spi.h>

#define SPI4_BASE             0x40013400ULL

#define SPI4_CR1_OFF          0x00
#define SPI4_CR2_OFF          0x04
#define SPI4_SR_OFF           0x08
#define SPI4_DR_OFF           0x0C

#define SPI_CR1_SPE           (1U << 6)

#define SPI_CR2_RXDMAEN       (1U << 0)
#define SPI_CR2_TXDMAEN       (1U << 1)

#define SPI_SR_RXNE           (1U << 0)
#define SPI_SR_TXE            (1U << 1)
#define SPI_SR_BSY            (1U << 7)

#define SPI4_DR_ADDR          (SPI4_BASE + SPI4_DR_OFF)
#define SPI4_RX_FIFO_SIZE     64
#define SPI4_DMA_BURST_SIZE   8

#define SPI4_CS_ICM20948_EXT  1
#define SPI4_CS_MS5611_EXT    2
#define SPI4_CS_ICM20602_EXT  4
#define SPI4_CS_UNUSED_3      3

#define GPIOC_PC13_SIGNAL_ID  45
#define GPIOC_PC14_SIGNAL_ID  46
#define GPIOC_PC15_SIGNAL_ID  47
#define GPIOE_PE4_SIGNAL_ID   68

#define DMA2_STREAM_SPI4_RX   3
#define DMA2_STREAM_SPI4_TX   4

typedef struct {
    SPIBus bus;
    uint32_t cr1;
    uint32_t cr2;
    uint8_t rx_fifo[SPI4_RX_FIFO_SIZE];
    unsigned rx_head;
    unsigned rx_tail;
    unsigned rx_count;
    int selected_logical_cs_id;
    int selected_actual_cs_id;
    bool explicit_cs_active;
    int actual_cs_icm20948_ext;
    int actual_cs_ms5611_ext;
    int actual_cs_icm20602_ext;
} SPI4State;

static SPI4State g_spi4;

static uint32_t spi4_mask_for_size(unsigned size) {
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    default:
        return 0xFFFFFFFFU;
    }
}

static uint32_t spi4_extract_reg32(uint32_t reg, uint64_t suboff, unsigned size) {
    uint32_t mask = spi4_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);

    if (size >= 4) {
        return reg;
    }
    return (reg >> shift) & mask;
}

static uint32_t spi4_merge_reg32(uint32_t oldv, uint64_t suboff, uint64_t value, unsigned size) {
    uint32_t mask = spi4_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);
    uint32_t fullmask = (size >= 4) ? 0xFFFFFFFFU : (mask << shift);

    if (size >= 4) {
        return (uint32_t)value;
    }
    return (oldv & ~fullmask) | ((((uint32_t)value) & mask) << shift);
}

static bool spi4_rx_push(SPI4State *s, uint8_t v) {
    if (s->rx_count >= SPI4_RX_FIFO_SIZE) {
        return false;
    }

    s->rx_fifo[s->rx_head] = v;
    s->rx_head = (s->rx_head + 1U) % SPI4_RX_FIFO_SIZE;
    s->rx_count++;
    return true;
}

static bool spi4_rx_pop(SPI4State *s, uint8_t *out) {
    if (s->rx_count == 0U) {
        return false;
    }

    *out = s->rx_fifo[s->rx_tail];
    s->rx_tail = (s->rx_tail + 1U) % SPI4_RX_FIFO_SIZE;
    s->rx_count--;
    return true;
}

static bool spi4_bus_has_cs(SPI4State *s, int cs_id) {
    int i;

    for (i = 0; i < s->bus.Slaves.num_slaves; i++) {
        if (s->bus.Slaves.slave[i].cs_id == cs_id) {
            return true;
        }
    }

    return false;
}

static int spi4_find_cs_by_name(SPI4State *s, const char *needle) {
    int i;

    if (needle == NULL) {
        return -1;
    }

    for (i = 0; i < s->bus.Slaves.num_slaves; i++) {
        const char *name = s->bus.Slaves.slave[i].name;

        if (name != NULL && strstr(name, needle) != NULL) {
            return s->bus.Slaves.slave[i].cs_id;
        }
    }

    return -1;
}

static int spi4_lookup_actual_cs(SPI4State *s, int logical_cs_id) {
    switch (logical_cs_id) {
    case SPI4_CS_ICM20948_EXT:
        return s->actual_cs_icm20948_ext;
    case SPI4_CS_MS5611_EXT:
        return s->actual_cs_ms5611_ext;
    case SPI4_CS_ICM20602_EXT:
        return s->actual_cs_icm20602_ext;
    default:
        return -1;
    }
}

static void spi4_set_all_cs_high(SPI4State *s) {
    int i;

    for (i = 0; i < s->bus.Slaves.num_slaves; i++) {
        api_spi_deselect(&s->bus, s->bus.Slaves.slave[i].cs_id);
    }

    s->selected_logical_cs_id = -1;
    s->selected_actual_cs_id = -1;
    s->explicit_cs_active = false;
}

static void spi4_maybe_request_tx_dma(SPI4State *s) {
    if (s == NULL) {
        return;
    }
    if ((s->cr1 & SPI_CR1_SPE) == 0U) {
        return;
    }
    if ((s->cr2 & SPI_CR2_TXDMAEN) == 0U) {
        return;
    }
    if (s->selected_actual_cs_id < 0) {
        return;
    }

    api_dma_request(2, DMA2_STREAM_SPI4_TX);
}

static void spi4_deselect_if_needed(SPI4State *s) {
    if (s == NULL) {
        return;
    }

    if (s->selected_actual_cs_id >= 0) {
        api_spi_deselect(&s->bus, s->selected_actual_cs_id);
    }

    s->selected_logical_cs_id = -1;
    s->selected_actual_cs_id = -1;
    s->explicit_cs_active = false;
}

static void spi4_select_logical_cs(SPI4State *s, int logical_cs_id, bool explicit_cs) {
    int actual_cs_id;

    if (s == NULL) {
        return;
    }

    actual_cs_id = spi4_lookup_actual_cs(s, logical_cs_id);
    if (actual_cs_id < 0) {
        return;
    }

    if (s->selected_logical_cs_id == logical_cs_id &&
        s->selected_actual_cs_id == actual_cs_id) {
        if (explicit_cs) {
            s->explicit_cs_active = true;
        }
        return;
    }

    if (!api_spi_select(&s->bus, actual_cs_id)) {
        return;
    }

    s->selected_logical_cs_id = logical_cs_id;
    s->selected_actual_cs_id = actual_cs_id;
    s->explicit_cs_active = explicit_cs;
}

static int spi4_signal_to_logical_cs(int signal_id) {
    switch (signal_id) {
    case GPIOC_PC13_SIGNAL_ID:
        return SPI4_CS_ICM20602_EXT;
    case GPIOC_PC14_SIGNAL_ID:
        return SPI4_CS_MS5611_EXT;
    case GPIOC_PC15_SIGNAL_ID:
        return SPI4_CS_UNUSED_3;
    case GPIOE_PE4_SIGNAL_ID:
        return SPI4_CS_ICM20948_EXT;
    default:
        return -1;
    }
}

static void spi4_cs_signal(void *opaque, int signal_id, bool level) {
    SPI4State *s = (SPI4State *)opaque;
    int logical_cs_id;

    if (s == NULL) {
        s = &g_spi4;
    }

    logical_cs_id = spi4_signal_to_logical_cs(signal_id);
    if (logical_cs_id < 0) {
        return;
    }

    if (!level) {
        spi4_select_logical_cs(s, logical_cs_id, true);
        spi4_maybe_request_tx_dma(s);
    } else if (s->selected_logical_cs_id == logical_cs_id && s->explicit_cs_active) {
        spi4_deselect_if_needed(s);
    }
}

static void spi4_route_rx_data(SPI4State *s, const uint8_t *rx_data, int len) {
    int pos = 0;
    int chunk;

    if (s == NULL || rx_data == NULL || len <= 0) {
        return;
    }

    if ((s->cr2 & SPI_CR2_RXDMAEN) != 0U) {
        while (pos < len) {
            chunk = len - pos;
            if (chunk > SPI4_DMA_BURST_SIZE) {
                chunk = SPI4_DMA_BURST_SIZE;
            }
            if (api_dma_request_data(2, DMA2_STREAM_SPI4_RX, SPI4_DR_ADDR, &rx_data[pos], chunk) < 0) {
                break;
            }
            pos += chunk;
        }
    }

    while (pos < len) {
        spi4_rx_push(s, rx_data[pos]);
        pos++;
    }
}

static uint8_t spi4_transfer_byte(SPI4State *s, uint8_t tx) {
    uint8_t rx = 0xFFU;

    if (s->selected_actual_cs_id >= 0) {
        rx = api_spi_transfer_byte(&s->bus, tx);
    }

    spi4_route_rx_data(s, &rx, 1);
    return rx;
}

static void spi4_dma_tx_handler(void *opaque, const uint8_t *data, int len) {
    SPI4State *s = (SPI4State *)opaque;
    uint8_t rxbuf[SPI4_DMA_BURST_SIZE];
    int pos;
    int chunk;
    int transferred;

    if (s == NULL) {
        s = &g_spi4;
    }
    if (data == NULL || len <= 0) {
        return;
    }
    if ((s->cr1 & SPI_CR1_SPE) == 0U) {
        return;
    }

    pos = 0;
    while (pos < len) {
        chunk = len - pos;
        if (chunk > SPI4_DMA_BURST_SIZE) {
            chunk = SPI4_DMA_BURST_SIZE;
        }

        memset(rxbuf, 0xFF, (size_t)chunk);
        if (s->selected_actual_cs_id >= 0) {
            transferred = api_spi_transfer_buf(&s->bus, &data[pos], rxbuf, (size_t)chunk);
            if (transferred < 0) {
                transferred = 0;
            }
            if (transferred < chunk) {
                memset(&rxbuf[transferred], 0xFF, (size_t)(chunk - transferred));
            }
        }

        spi4_route_rx_data(s, rxbuf, chunk);
        pos += chunk;
    }
}

void* spi4_init(ConfigSection* model_info) {
    memset(&g_spi4, 0, sizeof(g_spi4));
    g_spi4.bus = api_spi_init_bus(model_info, "spi4");

    g_spi4.selected_logical_cs_id = -1;
    g_spi4.selected_actual_cs_id = -1;
    g_spi4.explicit_cs_active = false;

    g_spi4.actual_cs_icm20948_ext = spi4_find_cs_by_name(&g_spi4, "icm20948");
    if (g_spi4.actual_cs_icm20948_ext < 0 && spi4_bus_has_cs(&g_spi4, SPI4_CS_ICM20948_EXT)) {
        g_spi4.actual_cs_icm20948_ext = SPI4_CS_ICM20948_EXT;
    }

    g_spi4.actual_cs_ms5611_ext = spi4_find_cs_by_name(&g_spi4, "ms5611");
    if (g_spi4.actual_cs_ms5611_ext < 0 && spi4_bus_has_cs(&g_spi4, SPI4_CS_MS5611_EXT)) {
        g_spi4.actual_cs_ms5611_ext = SPI4_CS_MS5611_EXT;
    }

    g_spi4.actual_cs_icm20602_ext = spi4_find_cs_by_name(&g_spi4, "icm20602");
    if (g_spi4.actual_cs_icm20602_ext < 0 && spi4_bus_has_cs(&g_spi4, SPI4_CS_ICM20602_EXT)) {
        g_spi4.actual_cs_icm20602_ext = SPI4_CS_ICM20602_EXT;
    }

    api_dma_register_stream_data(2, DMA2_STREAM_SPI4_TX, spi4_dma_tx_handler, &g_spi4);
    api_signal_register(GPIOC_PC13_SIGNAL_ID, spi4_cs_signal, &g_spi4);
    api_signal_register(GPIOC_PC14_SIGNAL_ID, spi4_cs_signal, &g_spi4);
    api_signal_register(GPIOC_PC15_SIGNAL_ID, spi4_cs_signal, &g_spi4);
    api_signal_register(GPIOE_PE4_SIGNAL_ID, spi4_cs_signal, &g_spi4);

    spi4_set_all_cs_high(&g_spi4);
    dev_debug("spi4: initialized with GPIOC/GPIOE chip-select routing and icm20948 support\n");
    return &g_spi4;
}

uint64_t spi4_read(void *opaque, uint64_t addr, unsigned size) {
    SPI4State *s = (SPI4State *)opaque;
    uint64_t offset = addr - SPI4_BASE;
    uint8_t rx = 0xFFU;
    uint32_t sr;

    if (s == NULL) {
        s = &g_spi4;
    }

    switch (offset & ~0x3ULL) {
    case SPI4_CR1_OFF:
        return spi4_extract_reg32(s->cr1, offset - SPI4_CR1_OFF, size);
    case SPI4_CR2_OFF:
        return spi4_extract_reg32(s->cr2, offset - SPI4_CR2_OFF, size);
    case SPI4_SR_OFF:
        sr = SPI_SR_TXE |
             ((s->rx_count > 0U) ? SPI_SR_RXNE : 0U) |
             ((s->selected_logical_cs_id >= 0) ? SPI_SR_BSY : 0U);
        return spi4_extract_reg32(sr, offset - SPI4_SR_OFF, size);
    case SPI4_DR_OFF:
        if (spi4_rx_pop(s, &rx)) {
            return spi4_extract_reg32((uint32_t)rx, offset - SPI4_DR_OFF, size);
        }
        return spi4_extract_reg32(0xFFU, offset - SPI4_DR_OFF, size);
    default:
        return 0;
    }
}

void spi4_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    SPI4State *s = (SPI4State *)opaque;
    uint64_t offset = addr - SPI4_BASE;

    if (s == NULL) {
        s = &g_spi4;
    }

    switch (offset & ~0x3ULL) {
    case SPI4_CR1_OFF:
        s->cr1 = spi4_merge_reg32(s->cr1, offset - SPI4_CR1_OFF, value, size);
        if ((s->cr1 & SPI_CR1_SPE) == 0U && !s->explicit_cs_active) {
            spi4_deselect_if_needed(s);
        } else {
            spi4_maybe_request_tx_dma(s);
        }
        return;
    case SPI4_CR2_OFF:
        s->cr2 = spi4_merge_reg32(s->cr2, offset - SPI4_CR2_OFF, value, size);
        spi4_maybe_request_tx_dma(s);
        return;
    case SPI4_DR_OFF: {
        unsigned i;
        unsigned bytes = size;

        if (bytes == 0U) {
            bytes = 1U;
        }
        if (bytes > 4U) {
            bytes = 4U;
        }

        if ((s->cr1 & SPI_CR1_SPE) == 0U) {
            return;
        }

        for (i = 0; i < bytes; i++) {
            spi4_transfer_byte(s, (uint8_t)((value >> (i * 8U)) & 0xFFU));
        }
        return;
    }
    default:
        return;
    }
}
