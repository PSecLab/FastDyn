#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define SPI4_BASE           0x40013400ULL

#define SPI4_CR1_OFF        0x00
#define SPI4_CR2_OFF        0x04
#define SPI4_SR_OFF         0x08
#define SPI4_DR_OFF         0x0C

#define SPI_CR1_SPE         (1U << 6)

#define SPI_CR2_RXDMAEN     (1U << 0)
#define SPI_CR2_TXDMAEN     (1U << 1)

#define SPI_SR_RXNE         (1U << 0)
#define SPI_SR_TXE          (1U << 1)
#define SPI_SR_BSY          (1U << 7)

#define SPI4_DR_ADDR        (SPI4_BASE + SPI4_DR_OFF)
#define SPI4_RX_FIFO_SIZE   64

typedef struct {
    SPIBus bus;
    uint16_t cr1;
    uint16_t cr2;
    uint8_t rx_fifo[SPI4_RX_FIFO_SIZE];
    unsigned rx_head;
    unsigned rx_tail;
    unsigned rx_count;
    bool cs_selected;
} SPI4State;

static SPI4State g_spi4;

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

static void spi4_select_if_needed(SPI4State *s) {
    if (!s->cs_selected) {
        api_spi_set_cs(&s->bus, 0, 0);
        s->cs_selected = true;
    }
}

static void spi4_deselect_if_needed(SPI4State *s) {
    if (s->cs_selected) {
        api_spi_set_cs(&s->bus, 0, 1);
        s->cs_selected = false;
    }
}

static uint8_t spi4_transfer_byte(SPI4State *s, uint8_t tx) {
    uint8_t rx;

    spi4_select_if_needed(s);
    rx = (uint8_t)(api_spi_transfer(&s->bus, tx) & 0xFFU);

    if ((s->cr2 & SPI_CR2_RXDMAEN) != 0U) {
        if (api_dma_request_data(2, 3, SPI4_DR_ADDR, &rx, 1) < 0) {
            spi4_rx_push(s, rx);
        }
    } else {
        spi4_rx_push(s, rx);
    }

    return rx;
}

static void spi4_dma_tx_handler(void *opaque, const uint8_t *data, int len) {
    SPI4State *s = (SPI4State *)opaque;
    int i;

    if (s == NULL || data == NULL || len <= 0) {
        return;
    }

    for (i = 0; i < len; i++) {
        spi4_transfer_byte(s, data[i]);
    }
}

void* spi4_init(ConfigSection* model_info) {
    memset(&g_spi4, 0, sizeof(g_spi4));
    g_spi4.bus = api_spi_init_bus(model_info, "spi4");
    api_dma_register_stream_data(2, 4, spi4_dma_tx_handler, &g_spi4);
    api_spi_set_cs(&g_spi4.bus, 0, 1);
    dev_debug("spi4: initialized with DMA2 stream4 TX sink and DMA2 stream3 RX source\n");
    return &g_spi4;
}

uint64_t spi4_read(void *opaque, uint64_t addr, unsigned size) {
    SPI4State *s = (SPI4State *)opaque;
    uint64_t offset = addr - SPI4_BASE;
    uint8_t rx = 0xFFU;

    (void)size;

    switch (offset) {
    case SPI4_CR1_OFF:
        return s->cr1;
    case SPI4_CR2_OFF:
        return s->cr2;
    case SPI4_SR_OFF:
        return SPI_SR_TXE |
               ((s->rx_count > 0U) ? SPI_SR_RXNE : 0U);
    case SPI4_DR_OFF:
        if (spi4_rx_pop(s, &rx)) {
            return rx;
        }
        return 0xFFU;
    default:
        return 0;
    }
}

void spi4_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    SPI4State *s = (SPI4State *)opaque;
    uint64_t offset = addr - SPI4_BASE;

    (void)size;

    switch (offset) {
    case SPI4_CR1_OFF:
        s->cr1 = (uint16_t)value;
        if ((s->cr1 & SPI_CR1_SPE) == 0U) {
            spi4_deselect_if_needed(s);
        }
        return;
    case SPI4_CR2_OFF:
        s->cr2 = (uint16_t)value;
        return;
    case SPI4_DR_OFF:
        if ((s->cr1 & SPI_CR1_SPE) != 0U) {
            spi4_transfer_byte(s, (uint8_t)value);
        }
        return;
    default:
        return;
    }
}
