// Device Model for I2C1
#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Inferred Register Functions:
// CTRL    : enable/control, with RO line-state bits SCL/SDA high when idle
// STATUS  : dynamic FIFO/bus status
// INTFL0  : sticky W1C transfer/progress flags, polled by firmware
// INTEN0  : stored interrupt enable
// INTFL1  : sticky W1C secondary/error flags
// INTEN1  : stored interrupt enable
// FIFOLEN : constant FIFO depths (8 RX / 8 TX)
// RXCTRL0 : RX FIFO flush + dynamic occupancy in bits [11:8]
// RXCTRL1 : configured receive count in bits [7:0], occupancy in [11:8]
// TXCTRL0 : TX FIFO flush + dynamic occupancy in bits [11:8]
// TXCTRL1 : TX FIFO status / preload-ready / occupancy
// FIFO    : write pushes TX data, read pops RX data
// MSTCTRL : master command trigger for queued I2C transactions

#define I2C1_BASE              0x4001E000ULL

#define I2C1_REG_CTRL          0x00
#define I2C1_REG_STATUS        0x04
#define I2C1_REG_INTFL0        0x08
#define I2C1_REG_INTEN0        0x0C
#define I2C1_REG_INTFL1        0x10
#define I2C1_REG_INTEN1        0x14
#define I2C1_REG_FIFOLEN       0x18
#define I2C1_REG_RXCTRL0       0x1C
#define I2C1_REG_RXCTRL1       0x20
#define I2C1_REG_TXCTRL0       0x24
#define I2C1_REG_TXCTRL1       0x28
#define I2C1_REG_FIFO          0x2C
#define I2C1_REG_MSTCTRL       0x30
#define I2C1_REG_CLKLO         0x34
#define I2C1_REG_CLKHI         0x38
#define I2C1_REG_HSCLK         0x3C
#define I2C1_REG_TIMEOUT       0x40
#define I2C1_REG_SLADDR        0x44
#define I2C1_REG_DMA           0x48

#define I2C1_FIFO_DEPTH        8

// CTRL bits inferred from SVD
#define I2C1_CTRL_EN           (1u << 0)
#define I2C1_CTRL_SCL_OUT      (1u << 6)
#define I2C1_CTRL_SDA_OUT      (1u << 7)
#define I2C1_CTRL_SCL_RO       (1u << 8)
#define I2C1_CTRL_SDA_RO       (1u << 9)
#define I2C1_CTRL_BB_MODE      (1u << 10)
#define I2C1_CTRL_READ_RO      (1u << 11)
#define I2C1_CTRL_RW_MASK      0x0000B4DFu

// STATUS bits from provided SVD summary
#define I2C1_STATUS_BUSY       (1u << 0)
#define I2C1_STATUS_RX_EM      (1u << 1)
#define I2C1_STATUS_RX_FULL    (1u << 2)
#define I2C1_STATUS_TX_EM      (1u << 3)
#define I2C1_STATUS_TX_FULL    (1u << 4)
#define I2C1_STATUS_MST_BUSY   (1u << 5)

// INTFL bits are inferred only to the extent needed by the trace
#define I2C1_INTFL0_DONE       0x01u
#define I2C1_INTFL0_PROGRESS   0x20u
#define I2C1_INTFL0_RX_AVAIL   0x40u
#define I2C1_INTFL0_STOP       0x80u
#define I2C1_INTFL1_ERROR      0x04u

// RX/TX CTRL0 trace shows bit 7 behaving like FIFO flush
#define I2C1_FIFO_FLUSH_BIT    0x80u

typedef struct {
    I2CBus bus;

    uint32_t ctrl;
    uint32_t status;
    uint32_t intfl0;
    uint32_t inten0;
    uint32_t intfl1;
    uint32_t inten1;

    uint32_t rxctrl0_cfg;
    uint32_t rxctrl1;
    uint32_t txctrl0_cfg;
    uint32_t txctrl1;

    uint32_t mstctrl;
    uint32_t clklo;
    uint32_t clkhi;
    uint32_t hsclk;
    uint32_t timeout;
    uint32_t sladdr;
    uint32_t dma;

    uint8_t tx_fifo[I2C1_FIFO_DEPTH];
    uint8_t rx_fifo[I2C1_FIFO_DEPTH];
    int tx_fifo_count;
    int rx_fifo_count;

    bool transfer_active;
    bool current_is_read;
    bool stop_after_read;
    uint16_t current_addr;

    uint32_t rx_expected;
    uint32_t rx_remaining;
} I2C1State;

static I2C1State g_i2c1;

static uint32_t i2c1_mask_by_size(uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        return (uint32_t)(value & 0xFFu);
    case 2:
        return (uint32_t)(value & 0xFFFFu);
    default:
        return (uint32_t)(value & 0xFFFFFFFFu);
    }
}

static void i2c1_update_status(I2C1State *s)
{
    uint32_t st = 0;

    if (s->transfer_active) {
        st |= I2C1_STATUS_BUSY;
        st |= I2C1_STATUS_MST_BUSY;
    }
    if (s->rx_fifo_count == 0) {
        st |= I2C1_STATUS_RX_EM;
    }
    if (s->rx_fifo_count >= I2C1_FIFO_DEPTH) {
        st |= I2C1_STATUS_RX_FULL;
    }
    if (s->tx_fifo_count == 0) {
        st |= I2C1_STATUS_TX_EM;
    }
    if (s->tx_fifo_count >= I2C1_FIFO_DEPTH) {
        st |= I2C1_STATUS_TX_FULL;
    }

    s->status = st;
}

static void i2c1_clear_tx_fifo(I2C1State *s)
{
    s->tx_fifo_count = 0;
    memset(s->tx_fifo, 0, sizeof(s->tx_fifo));
    i2c1_update_status(s);
}

static void i2c1_clear_rx_fifo(I2C1State *s)
{
    s->rx_fifo_count = 0;
    memset(s->rx_fifo, 0, sizeof(s->rx_fifo));
    i2c1_update_status(s);
}

static void i2c1_tx_push(I2C1State *s, uint8_t data)
{
    if (s->tx_fifo_count >= I2C1_FIFO_DEPTH) {
        return;
    }
    s->tx_fifo[s->tx_fifo_count++] = data;
    i2c1_update_status(s);
}

static uint8_t i2c1_tx_pop(I2C1State *s)
{
    uint8_t v = 0;
    int i;

    if (s->tx_fifo_count <= 0) {
        return 0;
    }

    v = s->tx_fifo[0];
    for (i = 1; i < s->tx_fifo_count; i++) {
        s->tx_fifo[i - 1] = s->tx_fifo[i];
    }
    s->tx_fifo_count--;
    i2c1_update_status(s);
    return v;
}

static void i2c1_rx_push(I2C1State *s, uint8_t data)
{
    if (s->rx_fifo_count >= I2C1_FIFO_DEPTH) {
        return;
    }
    s->rx_fifo[s->rx_fifo_count++] = data;
    i2c1_update_status(s);
}

static uint8_t i2c1_rx_pop(I2C1State *s)
{
    uint8_t v = 0;
    int i;

    if (s->rx_fifo_count <= 0) {
        return 0;
    }

    v = s->rx_fifo[0];
    for (i = 1; i < s->rx_fifo_count; i++) {
        s->rx_fifo[i - 1] = s->rx_fifo[i];
    }
    s->rx_fifo_count--;
    i2c1_update_status(s);
    return v;
}

static void i2c1_end_transfer(I2C1State *s)
{
    if (s->transfer_active) {
        api_i2c_end_transfer(&s->bus);
    }
    s->transfer_active = false;
    s->current_is_read = false;
    s->stop_after_read = false;
    s->current_addr = 0;
    s->rx_remaining = 0;
    i2c1_update_status(s);
}

static void i2c1_complete_error(I2C1State *s)
{
    s->intfl1 |= I2C1_INTFL1_ERROR;
    s->intfl0 |= (I2C1_INTFL0_DONE | I2C1_INTFL0_STOP);
    i2c1_end_transfer(s);
}

static int i2c1_start_transfer_from_fifo(I2C1State *s)
{
    uint8_t addr0;
    int ret;

    if (s->tx_fifo_count <= 0) {
        return -1;
    }

    addr0 = i2c1_tx_pop(s);

    // Best-effort 10-bit address detection
    if ((addr0 & 0xF8u) == 0xF0u) {
        uint8_t addr1;
        uint16_t addr10;
        bool is_recv;

        if (s->tx_fifo_count <= 0) {
            return -1;
        }

        addr1 = i2c1_tx_pop(s);
        addr10 = (uint16_t)(((addr0 & 0x06u) << 7) | addr1);
        is_recv = (addr0 & 0x01u) ? true : false;

        ret = api_i2c_start_transfer_10bit(&s->bus, addr10, is_recv);
        if (ret != 0) {
            return ret;
        }

        s->transfer_active = true;
        s->current_is_read = is_recv;
        s->current_addr = addr10;
        return 0;
    } else {
        uint8_t addr7 = (uint8_t)(addr0 >> 1);
        bool is_recv = (addr0 & 0x01u) ? true : false;

        ret = api_i2c_start_transfer(&s->bus, addr7, is_recv);
        if (ret != 0) {
            return ret;
        }

        s->transfer_active = true;
        s->current_is_read = is_recv;
        s->current_addr = addr7;
        return 0;
    }
}

static void i2c1_refill_rx_fifo(I2C1State *s)
{
    while (s->transfer_active &&
           s->current_is_read &&
           s->rx_remaining > 0 &&
           s->rx_fifo_count < I2C1_FIFO_DEPTH) {
        uint8_t b = api_i2c_recv(&s->bus);
        i2c1_rx_push(s, b);
        s->rx_remaining--;
    }

    if (s->rx_fifo_count > 0) {
        s->intfl0 |= I2C1_INTFL0_PROGRESS;
        s->intfl0 |= I2C1_INTFL0_RX_AVAIL;
    }

    if (s->rx_remaining == 0 && s->current_is_read) {
        s->intfl0 |= I2C1_INTFL0_DONE;
        if (s->stop_after_read) {
            s->intfl0 |= I2C1_INTFL0_STOP;
            i2c1_end_transfer(s);
        }
    }

    i2c1_update_status(s);
}

static void i2c1_process_master_cmd(I2C1State *s, uint32_t cmd)
{
    bool start = (cmd & 0x01u) ? true : false;
    bool restart = (cmd & 0x02u) ? true : false;
    bool stop = (cmd & 0x04u) ? true : false;

    s->mstctrl = cmd;

    // New master command starts a fresh visible status phase
    s->intfl0 &= ~(I2C1_INTFL0_DONE |
                   I2C1_INTFL0_PROGRESS |
                   I2C1_INTFL0_RX_AVAIL |
                   I2C1_INTFL0_STOP);
    s->intfl1 &= ~I2C1_INTFL1_ERROR;

    if ((s->ctrl & I2C1_CTRL_EN) == 0) {
        i2c1_update_status(s);
        return;
    }

    // Start/restart a phase when requested, or implicitly if idle and FIFO has an address
    if ((start || restart || !s->transfer_active) && s->tx_fifo_count > 0) {
        if (restart && s->transfer_active) {
            // Best-effort approximation of repeated start using end/start bus API sequence
            i2c1_end_transfer(s);
        }

        if (!s->transfer_active) {
            if (i2c1_start_transfer_from_fifo(s) != 0) {
                i2c1_complete_error(s);
                return;
            }
        }
    }

    if (!s->transfer_active) {
        if (stop) {
            s->intfl0 |= I2C1_INTFL0_STOP;
        }
        i2c1_update_status(s);
        return;
    }

    // Write phase: send all queued bytes
    if (!s->current_is_read) {
        while (s->tx_fifo_count > 0) {
            uint8_t b = i2c1_tx_pop(s);
            if (api_i2c_send(&s->bus, b) != 0) {
                i2c1_complete_error(s);
                return;
            }
        }

        // Matches observed "progress" polling state after a write phase
        s->intfl0 |= I2C1_INTFL0_PROGRESS;

        if (stop) {
            s->intfl0 |= I2C1_INTFL0_STOP;
            i2c1_end_transfer(s);
        }

        i2c1_update_status(s);
        return;
    }

    // Read phase: receive configured number of bytes
    s->rx_remaining = s->rx_expected;
    s->stop_after_read = stop;
    i2c1_refill_rx_fifo(s);
}

static uint32_t i2c1_ctrl_read(I2C1State *s)
{
    uint32_t v = (s->ctrl & I2C1_CTRL_RW_MASK);
    bool scl_high = true;
    bool sda_high = true;

    /*
     * CTRL[8] and CTRL[9] are live line-state bits, not latched echoes.
     * In software-output mode (BB_MODE/SWOE), the hardware reflects the
     * currently driven SCL_OUT/SDA_OUT levels. Otherwise, the trace shows
     * the bus idling high when the controller is not actively bit-banging.
     */
    if (v & I2C1_CTRL_BB_MODE) {
        scl_high = (v & I2C1_CTRL_SCL_OUT) ? true : false;
        sda_high = (v & I2C1_CTRL_SDA_OUT) ? true : false;
    }

    if (scl_high) {
        v |= I2C1_CTRL_SCL_RO;
    } else {
        v &= ~I2C1_CTRL_SCL_RO;
    }

    if (sda_high) {
        v |= I2C1_CTRL_SDA_RO;
    } else {
        v &= ~I2C1_CTRL_SDA_RO;
    }

    /*
     * CTRL.READ is a slave-side status bit (address/general-call match),
     * not the current master transaction direction. Keep it deasserted in
     * this master-only model unless future trace evidence requires more.
     */
    v &= ~I2C1_CTRL_READ_RO;

    return v;
}

static uint32_t i2c1_rxctrl0_read(I2C1State *s)
{
    return (s->rxctrl0_cfg & 0x7Fu) | ((uint32_t)(s->rx_fifo_count & 0xF) << 8);
}

static uint32_t i2c1_rxctrl1_read(I2C1State *s)
{
    return (s->rxctrl1 & 0xFFu) | ((uint32_t)(s->rx_fifo_count & 0xF) << 8);
}

static uint32_t i2c1_txctrl0_read(I2C1State *s)
{
    return (s->txctrl0_cfg & 0x7Fu) | ((uint32_t)(s->tx_fifo_count & 0xF) << 8);
}

static uint32_t i2c1_txctrl1_read(I2C1State *s)
{
    uint32_t v = 0;

    // PRELOAD_RDY when TX FIFO can accept data
    if (s->tx_fifo_count < I2C1_FIFO_DEPTH) {
        v |= 0x01u;
    }

    v |= (s->txctrl1 & 0xFEu);
    v |= ((uint32_t)(s->tx_fifo_count & 0xF) << 8);
    return v;
}

// This function will emulate all device reads
uint64_t i2c1_read(void *opaque, hwaddr addr, unsigned size)
{
    I2C1State *s = (I2C1State *)opaque;
    hwaddr offset = addr - I2C1_BASE;
    uint32_t v = 0;

    switch (offset) {
    case I2C1_REG_CTRL:
        v = i2c1_ctrl_read(s);
        break;
    case I2C1_REG_STATUS:
        i2c1_update_status(s);
        v = s->status;
        break;
    case I2C1_REG_INTFL0:
        v = s->intfl0;
        break;
    case I2C1_REG_INTEN0:
        v = s->inten0;
        break;
    case I2C1_REG_INTFL1:
        v = s->intfl1;
        break;
    case I2C1_REG_INTEN1:
        v = s->inten1;
        break;
    case I2C1_REG_FIFOLEN:
        v = 0x00000808u;
        break;
    case I2C1_REG_RXCTRL0:
        v = i2c1_rxctrl0_read(s);
        break;
    case I2C1_REG_RXCTRL1:
        v = i2c1_rxctrl1_read(s);
        break;
    case I2C1_REG_TXCTRL0:
        v = i2c1_txctrl0_read(s);
        break;
    case I2C1_REG_TXCTRL1:
        v = i2c1_txctrl1_read(s);
        break;
    case I2C1_REG_FIFO:
        if (s->rx_fifo_count > 0) {
            v = i2c1_rx_pop(s);
            if (s->transfer_active && s->current_is_read && s->rx_remaining > 0) {
                i2c1_refill_rx_fifo(s);
            }
        } else {
            v = 0;
        }
        break;
    case I2C1_REG_MSTCTRL:
        v = s->mstctrl;
        break;
    case I2C1_REG_CLKLO:
        v = s->clklo;
        break;
    case I2C1_REG_CLKHI:
        v = s->clkhi;
        break;
    case I2C1_REG_HSCLK:
        v = s->hsclk;
        break;
    case I2C1_REG_TIMEOUT:
        v = s->timeout;
        break;
    case I2C1_REG_SLADDR:
        v = s->sladdr;
        break;
    case I2C1_REG_DMA:
        v = s->dma;
        break;
    default:
        v = 0;
        break;
    }

    // Low-byte/halfword reads are returned from the low bits
    if (size == 1) {
        return v & 0xFFu;
    }
    if (size == 2) {
        return v & 0xFFFFu;
    }
    return v;
}

// This function will emulate all device writes
void i2c1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    I2C1State *s = (I2C1State *)opaque;
    hwaddr offset = addr - I2C1_BASE;
    uint32_t v = i2c1_mask_by_size(value, size);

    switch (offset) {
    case I2C1_REG_CTRL:
        s->ctrl = v & I2C1_CTRL_RW_MASK;
        // Disabling the peripheral terminates any active transfer
        if ((s->ctrl & I2C1_CTRL_EN) == 0) {
            i2c1_end_transfer(s);
        }
        break;

    case I2C1_REG_INTFL0:
        // W1C
        s->intfl0 &= ~v;
        break;

    case I2C1_REG_INTEN0:
        s->inten0 = v;
        break;

    case I2C1_REG_INTFL1:
        // W1C
        s->intfl1 &= ~v;
        break;

    case I2C1_REG_INTEN1:
        s->inten1 = v;
        break;

    case I2C1_REG_RXCTRL0:
        if (v & I2C1_FIFO_FLUSH_BIT) {
            i2c1_clear_rx_fifo(s);
        }
        s->rxctrl0_cfg = v & 0x7Fu;
        break;

    case I2C1_REG_RXCTRL1:
        s->rxctrl1 = v & 0xFFu;
        s->rx_expected = s->rxctrl1 & 0xFFu;
        break;

    case I2C1_REG_TXCTRL0:
        if (v & I2C1_FIFO_FLUSH_BIT) {
            i2c1_clear_tx_fifo(s);
        }
        s->txctrl0_cfg = v & 0x7Fu;
        break;

    case I2C1_REG_TXCTRL1:
        s->txctrl1 = v & 0xFFu;
        break;

    case I2C1_REG_FIFO:
        i2c1_tx_push(s, (uint8_t)(v & 0xFFu));
        break;

    case I2C1_REG_MSTCTRL:
        i2c1_process_master_cmd(s, v);
        break;

    case I2C1_REG_CLKLO:
        s->clklo = v;
        break;

    case I2C1_REG_CLKHI:
        s->clkhi = v;
        break;

    case I2C1_REG_HSCLK:
        s->hsclk = v;
        break;

    case I2C1_REG_TIMEOUT:
        s->timeout = v;
        break;

    case I2C1_REG_SLADDR:
        s->sladdr = v;
        break;

    case I2C1_REG_DMA:
        s->dma = v;
        break;

    default:
        break;
    }

    i2c1_update_status(s);
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* i2c1_init(ConfigSection* model_info)
{
    memset(&g_i2c1, 0, sizeof(g_i2c1));

    g_i2c1.bus = api_i2c_init_bus(model_info);

    // Power-on visible state from trace:
    // - CTRL reads back with SCL/SDA high (0x300) even before EN is set
    // - FIFOs empty
    // - INT flags clear
    g_i2c1.ctrl = 0;
    g_i2c1.status = I2C1_STATUS_RX_EM | I2C1_STATUS_TX_EM;
    g_i2c1.intfl0 = 0;
    g_i2c1.intfl1 = 0;
    g_i2c1.inten0 = 0;
    g_i2c1.inten1 = 0;
    g_i2c1.rxctrl0_cfg = 0;
    g_i2c1.rxctrl1 = 0;
    g_i2c1.txctrl0_cfg = 0;
    g_i2c1.txctrl1 = 0;
    g_i2c1.clklo = 0x95;
    g_i2c1.clkhi = 0x95;
    g_i2c1.hsclk = 0;
    g_i2c1.timeout = 0;
    g_i2c1.sladdr = 0;
    g_i2c1.dma = 0;
    g_i2c1.mstctrl = 0;
    g_i2c1.rx_expected = 0;
    g_i2c1.rx_remaining = 0;
    g_i2c1.transfer_active = false;
    g_i2c1.current_is_read = false;
    g_i2c1.stop_after_read = false;
    g_i2c1.current_addr = 0;

    i2c1_update_status(&g_i2c1);
    return &g_i2c1;
}