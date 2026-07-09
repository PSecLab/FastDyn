#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define LPUART6_BASE 0x40198000ULL

#define LPUART_REG_VERID   0x00
#define LPUART_REG_PARAM   0x04
#define LPUART_REG_GLOBAL  0x08
#define LPUART_REG_PINCFG  0x0C
#define LPUART_REG_BAUD    0x10
#define LPUART_REG_STAT    0x14
#define LPUART_REG_CTRL    0x18
#define LPUART_REG_DATA    0x1C
#define LPUART_REG_MATCH   0x20
#define LPUART_REG_MODIR   0x24
#define LPUART_REG_FIFO    0x28
#define LPUART_REG_WATER   0x2C

#define LPUART_CTRL_RE     (1u << 18)
#define LPUART_CTRL_TE     (1u << 19)
#define LPUART_CTRL_RIE    (1u << 21)
#define LPUART_CTRL_TIE    (1u << 23)
#define LPUART_CTRL_ORIE   (1u << 27)

#define LPUART_STAT_MA2F   (1u << 14)
#define LPUART_STAT_MA1F   (1u << 15)
#define LPUART_STAT_PF     (1u << 16)
#define LPUART_STAT_FE     (1u << 17)
#define LPUART_STAT_NF     (1u << 18)
#define LPUART_STAT_OR     (1u << 19)
#define LPUART_STAT_IDLE   (1u << 20)
#define LPUART_STAT_RDRF   (1u << 21)
#define LPUART_STAT_TC     (1u << 22)
#define LPUART_STAT_TDRE   (1u << 23)
#define LPUART_STAT_RXEDGIF (1u << 30)
#define LPUART_STAT_LBKDIF (1u << 31)

#define LPUART_FIFO_RXFE   (1u << 3)
#define LPUART_FIFO_TXFE   (1u << 7)
#define LPUART_FIFO_RXUFE  (1u << 8)
#define LPUART_FIFO_TXOFE  (1u << 9)
#define LPUART_FIFO_RXIDEN_MASK (7u << 10)
#define LPUART_FIFO_RXFLUSH (1u << 14)
#define LPUART_FIFO_TXFLUSH (1u << 15)
#define LPUART_FIFO_RXUF   (1u << 16)
#define LPUART_FIFO_TXOF   (1u << 17)
#define LPUART_FIFO_RXEMPT (1u << 22)
#define LPUART_FIFO_TXEMPT (1u << 23)

#define LPUART_STAT_W1C_MASK \
    (LPUART_STAT_MA2F | LPUART_STAT_MA1F | LPUART_STAT_PF | LPUART_STAT_FE | \
     LPUART_STAT_NF | LPUART_STAT_OR | LPUART_STAT_IDLE | \
     LPUART_STAT_RXEDGIF | LPUART_STAT_LBKDIF)

#define LPUART_FIFO_CFG_WRITE_MASK \
    (LPUART_FIFO_RXFE | LPUART_FIFO_TXFE | LPUART_FIFO_RXUFE | \
     LPUART_FIFO_TXOFE | LPUART_FIFO_RXIDEN_MASK)

#define LPUART_WATER_WRITE_MASK 0x00FF00FFu

#define LPUART6_RX_BUF_SIZE 256
#define LPUART6_TX_BUF_SIZE 1024

typedef struct {
    uint32_t verid;
    uint32_t param;
    uint32_t global;
    uint32_t pincfg;
    uint32_t baud;
    uint32_t ctrl;
    uint32_t match;
    uint32_t modir;
    uint32_t fifo_cfg;
    uint32_t fifo_latch;
    uint32_t water_cfg;
    uint32_t stat_latch;

    int pty_fd;
    uint64_t housekeeping_timer;

    uint8_t rx_buf[LPUART6_RX_BUF_SIZE];
    unsigned rx_head;
    unsigned rx_tail;
    unsigned rx_count;

    uint8_t tx_buf[LPUART6_TX_BUF_SIZE];
    unsigned tx_head;
    unsigned tx_tail;
    unsigned tx_count;
} LPUART6State;

static LPUART6State g_lpuart6;

static uint64_t lpuart6_mask_value(uint64_t value, unsigned size) {
    switch (size) {
    case 1:
        return value & 0xFFu;
    case 2:
        return value & 0xFFFFu;
    case 4:
        return value & 0xFFFFFFFFu;
    default:
        return value;
    }
}

static void lpuart6_rx_clear(LPUART6State *s) {
    s->rx_head = 0;
    s->rx_tail = 0;
    s->rx_count = 0;
}

static void lpuart6_tx_clear(LPUART6State *s) {
    s->tx_head = 0;
    s->tx_tail = 0;
    s->tx_count = 0;
}

static void lpuart6_reset_regs(LPUART6State *s) {
    s->verid = 0;
    s->param = 0;
    s->global = 0;
    s->pincfg = 0;
    s->baud = 0;
    s->ctrl = 0;
    s->match = 0;
    s->modir = 0;
    s->fifo_cfg = 0;
    s->fifo_latch = 0;
    s->water_cfg = 0;
    s->stat_latch = 0;
    lpuart6_rx_clear(s);
    lpuart6_tx_clear(s);
}

static bool lpuart6_rx_push(LPUART6State *s, uint8_t value) {
    if (s->rx_count >= LPUART6_RX_BUF_SIZE) {
        s->stat_latch |= LPUART_STAT_OR;
        return false;
    }

    s->rx_buf[s->rx_head] = value;
    s->rx_head = (s->rx_head + 1u) % LPUART6_RX_BUF_SIZE;
    s->rx_count++;
    return true;
}

static bool lpuart6_rx_pop(LPUART6State *s, uint8_t *value) {
    if (s->rx_count == 0) {
        return false;
    }

    *value = s->rx_buf[s->rx_tail];
    s->rx_tail = (s->rx_tail + 1u) % LPUART6_RX_BUF_SIZE;
    s->rx_count--;
    return true;
}

static bool lpuart6_tx_push(LPUART6State *s, uint8_t value) {
    if (s->tx_count >= LPUART6_TX_BUF_SIZE) {
        s->fifo_latch |= LPUART_FIFO_TXOF;
        return false;
    }

    s->tx_buf[s->tx_head] = value;
    s->tx_head = (s->tx_head + 1u) % LPUART6_TX_BUF_SIZE;
    s->tx_count++;
    return true;
}

static void lpuart6_drain_tx(LPUART6State *s) {
    while (s->pty_fd >= 0 && s->tx_count > 0) {
        int rc = api_pty_write_req(s->pty_fd, s->tx_buf[s->tx_tail]);
        if (rc == 1) {
            s->tx_tail = (s->tx_tail + 1u) % LPUART6_TX_BUF_SIZE;
            s->tx_count--;
        } else {
            break;
        }
    }
}

static void lpuart6_pump_rx(LPUART6State *s) {
    while (s->pty_fd >= 0 && s->rx_count < LPUART6_RX_BUF_SIZE) {
        uint8_t value = 0;
        int rc = api_pty_read_nonblock(s->pty_fd, &value);
        if (rc <= 0) {
            break;
        }
        if (!lpuart6_rx_push(s, value)) {
            break;
        }
    }
}

static void lpuart6_service_io(LPUART6State *s) {
    lpuart6_pump_rx(s);
    lpuart6_drain_tx(s);
}

static void lpuart6_housekeeping(void *opaque) {
    LPUART6State *s = (LPUART6State *)opaque;
    lpuart6_service_io(s);
}

static uint32_t lpuart6_stat_value(LPUART6State *s) {
    uint32_t value = s->stat_latch | LPUART_STAT_TDRE | LPUART_STAT_TC;

    if (s->rx_count > 0) {
        value |= LPUART_STAT_RDRF;
    }

    return value;
}

static uint32_t lpuart6_fifo_value(LPUART6State *s) {
    uint32_t value = s->fifo_cfg | s->fifo_latch;

    if (s->rx_count == 0) {
        value |= LPUART_FIFO_RXEMPT;
    }
    value |= LPUART_FIFO_TXEMPT;

    return value;
}

static uint32_t lpuart6_water_value(LPUART6State *s) {
    uint32_t value = s->water_cfg & LPUART_WATER_WRITE_MASK;
    value |= ((uint32_t)(s->rx_count & 0xFFu) << 24);
    return value;
}

void* lpuart6_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_lpuart6, 0, sizeof(g_lpuart6));
    g_lpuart6.pty_fd = api_pty_fd_gen("lpuart6");
    lpuart6_reset_regs(&g_lpuart6);
    g_lpuart6.housekeeping_timer =
        qemu_plugin_timer_new_period_ns(lpuart6_housekeeping, &g_lpuart6, 1000000ULL);

    return &g_lpuart6;
}

uint64_t lpuart6_read(void *opaque, uint64_t addr, unsigned size) {
    LPUART6State *s = (LPUART6State *)opaque;
    uint64_t offset;
    uint32_t value = 0;

    if (s == NULL) {
        s = &g_lpuart6;
    }

    if (addr < LPUART6_BASE) {
        return 0;
    }

    offset = addr - LPUART6_BASE;
    lpuart6_service_io(s);

    switch (offset) {
    case LPUART_REG_VERID:
        value = s->verid;
        break;
    case LPUART_REG_PARAM:
        value = s->param;
        break;
    case LPUART_REG_GLOBAL:
        value = s->global;
        break;
    case LPUART_REG_PINCFG:
        value = s->pincfg;
        break;
    case LPUART_REG_BAUD:
        value = s->baud;
        break;
    case LPUART_REG_STAT:
        value = lpuart6_stat_value(s);
        break;
    case LPUART_REG_CTRL:
        value = s->ctrl;
        break;
    case LPUART_REG_DATA: {
        uint8_t data = 0;
        if (lpuart6_rx_pop(s, &data)) {
            value = data;
        } else {
            value = 0;
        }
        break;
    }
    case LPUART_REG_MATCH:
        value = s->match;
        break;
    case LPUART_REG_MODIR:
        value = s->modir;
        break;
    case LPUART_REG_FIFO:
        value = lpuart6_fifo_value(s);
        break;
    case LPUART_REG_WATER:
        value = lpuart6_water_value(s);
        break;
    default:
        value = 0;
        break;
    }

    return lpuart6_mask_value(value, size);
}

void lpuart6_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    LPUART6State *s = (LPUART6State *)opaque;
    uint64_t offset;
    uint32_t v;

    if (s == NULL) {
        s = &g_lpuart6;
    }

    if (addr < LPUART6_BASE) {
        return;
    }

    offset = addr - LPUART6_BASE;
    v = (uint32_t)lpuart6_mask_value(value, size);

    lpuart6_service_io(s);

    switch (offset) {
    case LPUART_REG_GLOBAL:
        s->global = v;
        break;
    case LPUART_REG_PINCFG:
        s->pincfg = v;
        break;
    case LPUART_REG_BAUD:
        s->baud = v;
        break;
    case LPUART_REG_STAT:
        s->stat_latch &= ~(v & LPUART_STAT_W1C_MASK);
        break;
    case LPUART_REG_CTRL:
        s->ctrl = v;
        break;
    case LPUART_REG_DATA:
        lpuart6_tx_push(s, (uint8_t)(v & 0xFFu));
        break;
    case LPUART_REG_MATCH:
        s->match = v;
        break;
    case LPUART_REG_MODIR:
        s->modir = v;
        break;
    case LPUART_REG_FIFO:
        if (v & LPUART_FIFO_RXFLUSH) {
            lpuart6_rx_clear(s);
            s->fifo_latch &= ~LPUART_FIFO_RXUF;
        }
        if (v & LPUART_FIFO_TXFLUSH) {
            lpuart6_tx_clear(s);
            s->fifo_latch &= ~LPUART_FIFO_TXOF;
        }
        if (v & LPUART_FIFO_RXUF) {
            s->fifo_latch &= ~LPUART_FIFO_RXUF;
        }
        if (v & LPUART_FIFO_TXOF) {
            s->fifo_latch &= ~LPUART_FIFO_TXOF;
        }
        s->fifo_cfg = (s->fifo_cfg & ~LPUART_FIFO_CFG_WRITE_MASK) |
                      (v & LPUART_FIFO_CFG_WRITE_MASK);
        break;
    case LPUART_REG_WATER:
        s->water_cfg = (s->water_cfg & ~LPUART_WATER_WRITE_MASK) |
                       (v & LPUART_WATER_WRITE_MASK);
        break;
    default:
        break;
    }

    lpuart6_service_io(s);
}
