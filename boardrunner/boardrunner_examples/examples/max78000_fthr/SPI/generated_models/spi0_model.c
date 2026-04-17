// Device Model for SPI0

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define SPI0_BASE           0x400BE000ULL
#define SPI0_FIFO_DEPTH     32

// Register offsets
#define SPI0_REG_FIFO32     0x00
#define SPI0_REG_CTRL0      0x04
#define SPI0_REG_CTRL1      0x08
#define SPI0_REG_CTRL2      0x0C
#define SPI0_REG_SSTIME     0x10
#define SPI0_REG_CLKCTRL    0x14
#define SPI0_REG_DMA        0x1C
#define SPI0_REG_INTFL      0x20
#define SPI0_REG_INTEN      0x24
#define SPI0_REG_STAT       0x30

// Inferred hardware-owned/status bits
#define SPI0_CTRL2_READY_BIT    0x00000800u

// INTFL behavior inferred from trace:
// - bit 1 appears set even before activity (read as 0x2)
// - after transfers, additional bits cause 0x807
#define SPI0_INTFL_PERSISTENT   0x00000002u
#define SPI0_INTFL_XFER_BITS    0x00000805u

// DMA writable fields per provided SVD
#define BIT(x)                  (1u << (x))
#define SPI0_DMA_TX_THD_MASK    0x0000001Fu
#define SPI0_DMA_TX_FIFO_EN     BIT(6)
#define SPI0_DMA_TX_FLUSH       BIT(7)
#define SPI0_DMA_DMA_TX_EN      BIT(15)
#define SPI0_DMA_RX_THD_MASK    0x001F0000u
#define SPI0_DMA_RX_FIFO_EN     BIT(22)
#define SPI0_DMA_RX_FLUSH       BIT(23)
#define SPI0_DMA_DMA_RX_EN      BIT(31)
#define SPI0_DMA_WRITABLE_MASK  (SPI0_DMA_TX_THD_MASK | SPI0_DMA_TX_FIFO_EN | \
                                 SPI0_DMA_TX_FLUSH | SPI0_DMA_DMA_TX_EN |    \
                                 SPI0_DMA_RX_THD_MASK | SPI0_DMA_RX_FIFO_EN | \
                                 SPI0_DMA_RX_FLUSH | SPI0_DMA_DMA_RX_EN)

typedef struct {
    SPIBus bus;

    uint32_t ctrl0;
    uint32_t ctrl1;
    uint32_t ctrl2_rw;
    uint32_t sstime;
    uint32_t clkctrl;
    uint32_t dma_rw;
    uint32_t intfl_latched;
    uint32_t inten;
    uint32_t stat;

    uint32_t rx_fifo[SPI0_FIFO_DEPTH];
    uint8_t rx_head;
    uint8_t rx_tail;
    uint8_t rx_level;

    uint32_t last_tx;
    uint32_t last_rx;

    bool ready;
    int selected_cs;
} SPI0State;

static SPI0State g_spi0;

static uint64_t spi0_mask_read_value(uint32_t value, unsigned size)
{
    switch (size) {
    case 1:
        return value & 0xFFu;
    case 2:
        return value & 0xFFFFu;
    default:
        return value;
    }
}

static uint32_t spi0_mask_write_value(uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        return (uint32_t)(value & 0xFFu);
    case 2:
        return (uint32_t)(value & 0xFFFFu);
    default:
        return (uint32_t)value;
    }
}

static void spi0_debug_unknown(const char *kind, hwaddr offset, uint64_t value, unsigned size)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "spi0: unknown %s offset=0x%llx value=0x%llx size=%u",
             kind,
             (unsigned long long)offset,
             (unsigned long long)value,
             size);
    dev_debug(buf);
}

static void spi0_rx_fifo_clear(SPI0State *s)
{
    s->rx_head = 0;
    s->rx_tail = 0;
    s->rx_level = 0;
}

static void spi0_rx_fifo_push(SPI0State *s, uint32_t value)
{
    if (s->rx_level >= SPI0_FIFO_DEPTH) {
        // Drop oldest to keep forward progress.
        s->rx_head = (uint8_t)((s->rx_head + 1) % SPI0_FIFO_DEPTH);
        s->rx_level--;
    }

    s->rx_fifo[s->rx_tail] = value;
    s->rx_tail = (uint8_t)((s->rx_tail + 1) % SPI0_FIFO_DEPTH);
    s->rx_level++;
}

static bool spi0_rx_fifo_pop(SPI0State *s, uint32_t *value)
{
    if (s->rx_level == 0) {
        return false;
    }

    *value = s->rx_fifo[s->rx_head];
    s->rx_head = (uint8_t)((s->rx_head + 1) % SPI0_FIFO_DEPTH);
    s->rx_level--;
    return true;
}

static uint32_t spi0_compose_ctrl2(SPI0State *s)
{
    uint32_t v = s->ctrl2_rw;
    if (s->ready) {
        v |= SPI0_CTRL2_READY_BIT;
    } else {
        v &= ~SPI0_CTRL2_READY_BIT;
    }
    return v;
}

static uint32_t spi0_compose_intfl(SPI0State *s)
{
    return s->intfl_latched | SPI0_INTFL_PERSISTENT;
}

static uint32_t spi0_compose_dma(SPI0State *s)
{
    uint32_t v = s->dma_rw;

    // Self-clearing flush bits do not read back as set.
    v &= ~SPI0_DMA_TX_FLUSH;
    v &= ~SPI0_DMA_RX_FLUSH;

    // RO FIFO level fields
    v &= ~((uint32_t)0x3Fu << 8);
    v &= ~((uint32_t)0x3Fu << 24);

    // TX is modeled as immediately drained into the bus on write.
    v |= ((uint32_t)0 & 0x3Fu) << 8;
    v |= ((uint32_t)s->rx_level & 0x3Fu) << 24;

    return v;
}

static uint32_t spi0_bus_transfer(SPI0State *s, uint32_t tx)
{
    /*
     * Use the configured SPI bus when a slave exists.
     * If no slave is configured, echo MOSI back on MISO as a conservative
     * fallback so polling firmware still makes progress.
     */
    if (s->bus.Slaves.num_slaves > 0) {
        api_spi_set_cs(&s->bus, s->selected_cs, 0);
        uint32_t rx = api_spi_transfer(&s->bus, tx);
        api_spi_set_cs(&s->bus, s->selected_cs, 1);
        return rx;
    }

    return tx;
}

static void spi0_note_transfer_complete(SPI0State *s)
{
    s->ready = true;
    s->intfl_latched |= SPI0_INTFL_XFER_BITS;
}

// This function will emulate all device reads
uint64_t spi0_read(void *opaque, hwaddr addr, unsigned size)
{
    SPI0State *s = (SPI0State *)opaque;
    hwaddr offset = addr - SPI0_BASE;
    uint32_t value = 0;

    switch (offset) {
    case SPI0_REG_FIFO32: {
        uint32_t rx;
        if (spi0_rx_fifo_pop(s, &rx)) {
            s->last_rx = rx;
            value = rx;
        } else {
            // Conservative empty-FIFO behavior.
            value = s->last_rx;
        }
        break;
    }

    case SPI0_REG_CTRL0:
        value = s->ctrl0;
        break;

    case SPI0_REG_CTRL1:
        value = s->ctrl1;
        break;

    case SPI0_REG_CTRL2:
        value = spi0_compose_ctrl2(s);
        break;

    case SPI0_REG_SSTIME:
        value = s->sstime;
        break;

    case SPI0_REG_CLKCTRL:
        value = s->clkctrl;
        break;

    case SPI0_REG_DMA:
        value = spi0_compose_dma(s);
        break;

    case SPI0_REG_INTFL:
        value = spi0_compose_intfl(s);
        break;

    case SPI0_REG_INTEN:
        value = s->inten;
        break;

    case SPI0_REG_STAT:
        value = s->stat;
        break;

    default:
        spi0_debug_unknown("read", offset, 0, size);
        value = 0;
        break;
    }

    return spi0_mask_read_value(value, size);
}

// This function will emulate all device writes
void spi0_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    SPI0State *s = (SPI0State *)opaque;
    hwaddr offset = addr - SPI0_BASE;
    uint32_t v = spi0_mask_write_value(value, size);

    switch (offset) {
    case SPI0_REG_FIFO32: {
        uint32_t rx;

        s->last_tx = v;
        rx = spi0_bus_transfer(s, v);
        s->last_rx = rx;
        spi0_rx_fifo_push(s, rx);
        spi0_note_transfer_complete(s);
        break;
    }

    case SPI0_REG_CTRL0:
        /*
         * Trace shows a write of 0x10003 later reading back as 0x10002.
         * Reproduce that narrow observed self-clear of bit 0 when bit 16 is
         * written, while otherwise behaving as a normal RW register.
         */
        s->ctrl0 = v;
        if (v & 0x00010000u) {
            s->ctrl0 &= ~0x1u;
            s->ready = true;
        }
        break;

    case SPI0_REG_CTRL1:
        s->ctrl1 = v;
        break;

    case SPI0_REG_CTRL2:
        /*
         * Treat bit 0x800 as hardware-owned status; keep all other bits RW.
         */
        s->ctrl2_rw = v & ~SPI0_CTRL2_READY_BIT;
        break;

    case SPI0_REG_SSTIME:
        s->sstime = v;
        break;

    case SPI0_REG_CLKCTRL:
        s->clkctrl = v;
        break;

    case SPI0_REG_DMA:
        s->dma_rw = v & SPI0_DMA_WRITABLE_MASK;

        if (v & SPI0_DMA_TX_FLUSH) {
            // TX is modeled as immediately drained; nothing buffered to clear.
            s->dma_rw &= ~SPI0_DMA_TX_FLUSH;
        }

        if (v & SPI0_DMA_RX_FLUSH) {
            spi0_rx_fifo_clear(s);
            s->dma_rw &= ~SPI0_DMA_RX_FLUSH;
        }
        break;

    case SPI0_REG_INTFL:
        /*
         * Write-one-to-clear latched activity bits.
         * The persistent 0x2 status is hardware-owned and is not stored in
         * intfl_latched, so clearing it here has no effect.
         */
        s->intfl_latched &= ~v;
        break;

    case SPI0_REG_INTEN:
        s->inten = v;
        break;

    case SPI0_REG_STAT:
        // Hardware status register; ignore software writes.
        break;

    default:
        spi0_debug_unknown("write", offset, value, size);
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* spi0_init(ConfigSection* model_info)
{
    memset(&g_spi0, 0, sizeof(g_spi0));

    g_spi0.bus = api_spi_init_bus(model_info);
    g_spi0.selected_cs = 0;

    // Reset-like defaults inferred from trace.
    g_spi0.ctrl0 = 0x00000000u;
    g_spi0.ctrl1 = 0x00000000u;
    g_spi0.ctrl2_rw = 0x00000000u;
    g_spi0.sstime = 0x00000000u;
    g_spi0.clkctrl = 0x00000000u;
    g_spi0.dma_rw = 0x00000000u;
    g_spi0.intfl_latched = 0x00000000u;   // read path adds persistent 0x2
    g_spi0.inten = 0x00000000u;
    g_spi0.stat = 0x00000000u;
    g_spi0.ready = false;
    g_spi0.last_tx = 0;
    g_spi0.last_rx = 0;

    spi0_rx_fifo_clear(&g_spi0);

    return &g_spi0;
}
