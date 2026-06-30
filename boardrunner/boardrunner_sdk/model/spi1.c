#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>
#include <boardrunner/spi.h>

#define SPI1_BASE               0x40013000ULL

#define SPI1_CR1_OFF            0x00
#define SPI1_CR2_OFF            0x04
#define SPI1_SR_OFF             0x08
#define SPI1_DR_OFF             0x0C

#define SPI_CR1_SPE             (1U << 6)

#define SPI_CR2_RXDMAEN         (1U << 0)
#define SPI_CR2_TXDMAEN         (1U << 1)

#define SPI_SR_RXNE             (1U << 0)
#define SPI_SR_TXE              (1U << 1)
#define SPI_SR_BSY              (1U << 7)

#define SPI1_DR_ADDR            (SPI1_BASE + SPI1_DR_OFF)
#define SPI1_RX_FIFO_SIZE       64

#define SPI1_CS_PD7_SIGNAL_ID   55
#define SPI1_CS_PC2_SIGNAL_ID   34
#define SPI1_CS_PC14_SIGNAL_ID  46

#define SPI1_CS_MS5611_ID       3
#define SPI1_CS_MPU9250_ID      4

#define DMA2_STREAM_SPI1_RX     2
#define DMA2_STREAM_SPI1_TX     5

typedef struct {
    SPIBus bus;
    uint16_t cr1;
    uint16_t cr2;
    uint8_t rx_fifo[SPI1_RX_FIFO_SIZE];
    unsigned rx_head;
    unsigned rx_tail;
    unsigned rx_count;
    int selected_cs_id;
    int fallback_cs_id;
    bool cs_selected;
    bool fallback_selected;
} SPI1State;

static SPI1State g_spi1;

static bool spi1_rx_push(SPI1State *s, uint8_t v) {
    if (s->rx_count >= SPI1_RX_FIFO_SIZE) {
        return false;
    }

    s->rx_fifo[s->rx_head] = v;
    s->rx_head = (s->rx_head + 1U) % SPI1_RX_FIFO_SIZE;
    s->rx_count++;
    return true;
}

static bool spi1_rx_pop(SPI1State *s, uint8_t *out) {
    if (s->rx_count == 0U) {
        return false;
    }

    *out = s->rx_fifo[s->rx_tail];
    s->rx_tail = (s->rx_tail + 1U) % SPI1_RX_FIFO_SIZE;
    s->rx_count--;
    return true;
}

static bool spi1_has_cs_id(SPI1State *s, int cs_id) {
    int i;

    if (s == NULL || cs_id < 0) {
        return false;
    }

    for (i = 0; i < s->bus.Slaves.num_slaves; i++) {
        if (s->bus.Slaves.slave[i].cs_id == cs_id) {
            return true;
        }
    }

    return false;
}

static int spi1_signal_to_cs_id(SPI1State *s, int signal_id) {
    switch (signal_id) {
    case SPI1_CS_PD7_SIGNAL_ID:
        if (spi1_has_cs_id(s, SPI1_CS_MS5611_ID)) {
            return SPI1_CS_MS5611_ID;
        }
        break;
    case SPI1_CS_PC2_SIGNAL_ID:
    case SPI1_CS_PC14_SIGNAL_ID:
        if (spi1_has_cs_id(s, SPI1_CS_MPU9250_ID)) {
            return SPI1_CS_MPU9250_ID;
        }
        break;
    default:
        break;
    }

    return -1;
}

static void spi1_set_all_cs_inactive(SPI1State *s) {
    int i;

    if (s == NULL) {
        return;
    }

    for (i = 0; i < s->bus.Slaves.num_slaves; i++) {
        api_spi_set_cs(&s->bus, s->bus.Slaves.slave[i].cs_id, 1);
    }

    s->selected_cs_id = -1;
    s->cs_selected = false;
    s->fallback_selected = false;
}

static void spi1_select_cs(SPI1State *s, int cs_id) {
    if (s == NULL || !spi1_has_cs_id(s, cs_id)) {
        return;
    }

    if (s->cs_selected && s->selected_cs_id == cs_id) {
        return;
    }

    spi1_set_all_cs_inactive(s);
    api_spi_set_cs(&s->bus, cs_id, 0);
    s->selected_cs_id = cs_id;
    s->cs_selected = true;
}

static void spi1_deselect_if_needed(SPI1State *s) {
    if (s == NULL || !s->cs_selected || s->selected_cs_id < 0) {
        return;
    }

    api_spi_set_cs(&s->bus, s->selected_cs_id, 1);
    s->selected_cs_id = -1;
    s->cs_selected = false;
    s->fallback_selected = false;
}

static void spi1_select_if_needed(SPI1State *s) {
    if (s == NULL || s->cs_selected) {
        return;
    }

    if (s->fallback_cs_id < 0) {
        return;
    }

    spi1_select_cs(s, s->fallback_cs_id);
    if (s->cs_selected) {
        s->fallback_selected = true;
    }
}

static void spi1_cs_signal(void *opaque, int signal_id, bool level) {
    SPI1State *s = (SPI1State *)opaque;
    int cs_id;

    if (s == NULL) {
        s = &g_spi1;
    }

    cs_id = spi1_signal_to_cs_id(s, signal_id);
    if (cs_id < 0) {
        return;
    }

    if (!level) {
        spi1_select_cs(s, cs_id);
        s->fallback_selected = false;
        return;
    }

    api_spi_set_cs(&s->bus, cs_id, 1);
    if (s->cs_selected && s->selected_cs_id == cs_id) {
        s->selected_cs_id = -1;
        s->cs_selected = false;
        s->fallback_selected = false;
    }
}

static uint8_t spi1_transfer_byte(SPI1State *s, uint8_t tx) {
    uint8_t rx = 0xFFU;

    if (s == NULL) {
        return 0xFFU;
    }

    spi1_select_if_needed(s);
    if (s->cs_selected) {
        rx = (uint8_t)(api_spi_transfer(&s->bus, tx) & 0xFFU);
    }

    if ((s->cr2 & SPI_CR2_RXDMAEN) != 0U) {
        if (api_dma_request_data(2, DMA2_STREAM_SPI1_RX, SPI1_DR_ADDR, &rx, 1) < 0) {
            spi1_rx_push(s, rx);
        }
    } else {
        spi1_rx_push(s, rx);
    }

    return rx;
}

static void spi1_dma_tx_handler(void *opaque, const uint8_t *data, int len) {
    SPI1State *s = (SPI1State *)opaque;
    int i;

    if (s == NULL) {
        s = &g_spi1;
    }
    if (data == NULL || len <= 0) {
        return;
    }

    if ((s->cr1 & SPI_CR1_SPE) == 0U) {
        return;
    }

    for (i = 0; i < len; i++) {
        spi1_transfer_byte(s, data[i]);
    }
}

void* spi1_init(ConfigSection* model_info) {
    memset(&g_spi1, 0, sizeof(g_spi1));
    g_spi1.bus = api_spi_init_bus(model_info, "spi1");
    g_spi1.selected_cs_id = -1;
    g_spi1.fallback_cs_id = -1;

    if (spi1_has_cs_id(&g_spi1, SPI1_CS_MS5611_ID)) {
        g_spi1.fallback_cs_id = SPI1_CS_MS5611_ID;
    } else if (g_spi1.bus.Slaves.num_slaves > 0) {
        g_spi1.fallback_cs_id = g_spi1.bus.Slaves.slave[0].cs_id;
    } else {
        dev_debug("spi1: initialized without attached SPI slaves");
    }

    spi1_set_all_cs_inactive(&g_spi1);
    api_signal_register(SPI1_CS_PD7_SIGNAL_ID, spi1_cs_signal, &g_spi1);
    api_signal_register(SPI1_CS_PC2_SIGNAL_ID, spi1_cs_signal, &g_spi1);
    api_signal_register(SPI1_CS_PC14_SIGNAL_ID, spi1_cs_signal, &g_spi1);
    api_dma_register_stream_data(2, DMA2_STREAM_SPI1_TX, spi1_dma_tx_handler, &g_spi1);
    dev_debug("spi1: initialized with CS routing for PD7->cs3 and PC2->cs4\n");
    return &g_spi1;
}

uint64_t spi1_read(void *opaque, uint64_t addr, unsigned size) {
    SPI1State *s = (SPI1State *)opaque;
    uint64_t offset = addr - SPI1_BASE;
    uint8_t rx = 0xFFU;

    if (s == NULL) {
        s = &g_spi1;
    }

    (void)size;

    switch (offset) {
    case SPI1_CR1_OFF:
        return s->cr1;
    case SPI1_CR2_OFF:
        return s->cr2;
    case SPI1_SR_OFF:
        return SPI_SR_TXE |
               ((s->cs_selected && (s->cr1 & SPI_CR1_SPE) != 0U) ? SPI_SR_BSY : 0U) |
               ((s->rx_count > 0U) ? SPI_SR_RXNE : 0U);
    case SPI1_DR_OFF:
        if (spi1_rx_pop(s, &rx)) {
            return rx;
        }
        return 0xFFU;
    default:
        return 0;
    }
}

void spi1_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    SPI1State *s = (SPI1State *)opaque;
    uint64_t offset = addr - SPI1_BASE;

    if (s == NULL) {
        s = &g_spi1;
    }

    (void)size;

    switch (offset) {
    case SPI1_CR1_OFF:
        s->cr1 = (uint16_t)value;
        if ((s->cr1 & SPI_CR1_SPE) == 0U) {
            spi1_deselect_if_needed(s);
        }
        return;
    case SPI1_CR2_OFF:
        s->cr2 = (uint16_t)value;
        return;
    case SPI1_DR_OFF:
        if ((s->cr1 & SPI_CR1_SPE) != 0U) {
            spi1_transfer_byte(s, (uint8_t)value);
        }
        return;
    default:
        return;
    }
}
