#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define SPI1_BASE           0x40013000ULL

#define SPI1_CR1_OFF        0x00
#define SPI1_CR2_OFF        0x04
#define SPI1_SR_OFF         0x08
#define SPI1_DR_OFF         0x0C

#define SPI_CR1_SPE         (1U << 6)

#define SPI_CR2_RXDMAEN     (1U << 0)
#define SPI_CR2_TXDMAEN     (1U << 1)

#define SPI_SR_RXNE         (1U << 0)
#define SPI_SR_TXE          (1U << 1)
#define SPI_SR_BSY          (1U << 7)

#define SPI1_DR_ADDR        (SPI1_BASE + SPI1_DR_OFF)
#define SPI1_RX_FIFO_SIZE   64

typedef struct {
    SPIBus bus;
    uint16_t cr1;
    uint16_t cr2;
    uint8_t rx_fifo[SPI1_RX_FIFO_SIZE];
    unsigned rx_head;
    unsigned rx_tail;
    unsigned rx_count;
    bool cs_selected;
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

static void spi1_select_if_needed(SPI1State *s) {
    if (!s->cs_selected) {
        api_spi_set_cs(&s->bus, 0, 0);
        s->cs_selected = true;
    }
}

static void spi1_deselect_if_needed(SPI1State *s) {
    if (s->cs_selected) {
        api_spi_set_cs(&s->bus, 0, 1);
        s->cs_selected = false;
    }
}

static uint8_t spi1_transfer_byte(SPI1State *s, uint8_t tx) {
    uint8_t rx;

    spi1_select_if_needed(s);
    rx = (uint8_t)(api_spi_transfer(&s->bus, tx) & 0xFFU);

    if ((s->cr2 & SPI_CR2_RXDMAEN) != 0U) {
        if (api_dma_request_data(2, 2, SPI1_DR_ADDR, &rx, 1) < 0) {
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

    if (s == NULL || data == NULL || len <= 0) {
        return;
    }

    for (i = 0; i < len; i++) {
        spi1_transfer_byte(s, data[i]);
    }
}

void* spi1_init(ConfigSection* model_info) {
    memset(&g_spi1, 0, sizeof(g_spi1));
    g_spi1.bus = api_spi_init_bus(model_info, "spi1");
    api_dma_register_stream_data(2, 5, spi1_dma_tx_handler, &g_spi1);
    api_spi_set_cs(&g_spi1.bus, 0, 1);
    dev_debug("spi1: initialized with DMA2 stream5 TX sink and DMA2 stream2 RX source\n");
    return &g_spi1;
}

uint64_t spi1_read(void *opaque, uint64_t addr, unsigned size) {
    SPI1State *s = (SPI1State *)opaque;
    uint64_t offset = addr - SPI1_BASE;
    uint8_t rx = 0xFFU;

    (void)size;

    switch (offset) {
    case SPI1_CR1_OFF:
        return s->cr1;
    case SPI1_CR2_OFF:
        return s->cr2;
    case SPI1_SR_OFF:
        return SPI_SR_TXE |
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
