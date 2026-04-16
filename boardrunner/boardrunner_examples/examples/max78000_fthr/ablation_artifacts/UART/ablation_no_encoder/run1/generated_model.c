// Device Model for UART
//
// Inferred Register Functions:
//   0x00 CTRL   : UART enable/configuration register
//   0x04 STATUS : Polled status; 0x50 when TX path is ready, 0x140 when RX data exists
//   0x10 CLKDIV : Baud/clock divider style register
//   0x14 CFG    : Secondary timing/config register
//   0x20 FIFO   : Shared TX/RX data register
//
// This model is intentionally minimal and trace-driven:
// - It emulates the observed initialization/readback behavior.
// - TX bytes are written to a host PTY.
// - RX bytes are polled from a host PTY into a small FIFO.
// - No UART interrupt MMIO registers were observed, so this model is polling-oriented.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define UART_BASE           0x40042000ULL

#define UART_REG_CTRL       0x00
#define UART_REG_STATUS     0x04
#define UART_REG_CLKDIV     0x10
#define UART_REG_CFG        0x14
#define UART_REG_FIFO       0x20

/* Observed status values */
#define UART_STATUS_TX_READY    0x00000050u
#define UART_STATUS_RX_READY    0x00000140u

/* Observed CTRL readback quirk:
 * write 0x00028c01 -> read 0x000a8c01
 * We model this as a synthesized hardware bit when 0x00008000 is set.
 */
#define UART_CTRL_SYNTH_IN      0x00008000u
#define UART_CTRL_SYNTH_OUT     0x00080000u

#define UART_ENABLE_BIT         0x00000001u

#define UART_RX_FIFO_SIZE       256
#define UART_POLL_PERIOD_NS     1000000ULL   /* 1 ms periodic poll */

typedef struct UARTState {
    uint32_t ctrl;
    uint32_t clkdiv;
    uint32_t cfg;

    uint8_t rx_fifo[UART_RX_FIFO_SIZE];
    unsigned rx_head;
    unsigned rx_tail;
    unsigned rx_count;

    uint8_t last_tx;
    uint8_t last_rx;

    int pty_fd;
    uint64_t poll_timer;
} UARTState;

static UARTState g_uart;

static void uart_log(const char *msg)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "[max78000-uart] %s", msg);
    dev_debug(buf);
}

static uint64_t uart_mask_by_size(uint64_t v, unsigned size)
{
    switch (size) {
    case 1:
        return v & 0xffu;
    case 2:
        return v & 0xffffu;
    case 4:
    default:
        return v & 0xffffffffu;
    }
}

static bool uart_rx_push(UARTState *s, uint8_t v)
{
    if (s->rx_count >= UART_RX_FIFO_SIZE) {
        return false;
    }

    s->rx_fifo[s->rx_tail] = v;
    s->rx_tail = (s->rx_tail + 1) % UART_RX_FIFO_SIZE;
    s->rx_count++;
    return true;
}

static bool uart_rx_pop(UARTState *s, uint8_t *out)
{
    if (s->rx_count == 0) {
        return false;
    }

    *out = s->rx_fifo[s->rx_head];
    s->rx_head = (s->rx_head + 1) % UART_RX_FIFO_SIZE;
    s->rx_count--;
    return true;
}

static uint32_t uart_ctrl_readback(UARTState *s)
{
    uint32_t v = s->ctrl;

    if (v & UART_CTRL_SYNTH_IN) {
        v |= UART_CTRL_SYNTH_OUT;
    }

    return v;
}

static uint32_t uart_status_readback(UARTState *s)
{
    if (s->rx_count > 0) {
        return UART_STATUS_RX_READY;
    }

    return UART_STATUS_TX_READY;
}

static void uart_poll_rx_cb(void *opaque)
{
    UARTState *s = (UARTState *)opaque;
    uint8_t ch;

    if (s == NULL) {
        return;
    }

    if (s->pty_fd < 0) {
        return;
    }

    /* Trace shows UART used only after enable. Avoid collecting data before that. */
    if ((s->ctrl & UART_ENABLE_BIT) == 0) {
        return;
    }

    while (s->rx_count < UART_RX_FIFO_SIZE) {
        int rc = api_pty_read_nonblock(s->pty_fd, &ch);
        if (rc <= 0) {
            break;
        }
        uart_rx_push(s, ch);
    }
}

// This function will emulate all device reads
uint64_t uart_read(void *opaque, hwaddr addr, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    uint64_t ret = 0;

    switch (offset) {
    case UART_REG_CTRL:
        ret = uart_ctrl_readback(s);
        break;

    case UART_REG_STATUS:
        ret = uart_status_readback(s);
        break;

    case UART_REG_CLKDIV:
        ret = s->clkdiv;
        break;

    case UART_REG_CFG:
        ret = s->cfg;
        break;

    case UART_REG_FIFO: {
        uint8_t ch = 0;
        if (uart_rx_pop(s, &ch)) {
            s->last_rx = ch;
            ret = ch;
        } else {
            ret = 0;
        }
        break;
    }

    default: {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "read from unknown offset 0x%llx (abs=0x%llx)",
                 (unsigned long long)offset,
                 (unsigned long long)addr);
        dev_debug(buf);
        ret = 0;
        break;
    }
    }

    return uart_mask_by_size(ret, size);
}

// This function will emulate all device writes
void uart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    (void)size;

    switch (offset) {
    case UART_REG_CTRL:
        s->ctrl = (uint32_t)value;
        break;

    case UART_REG_CLKDIV:
        s->clkdiv = (uint32_t)value;
        break;

    case UART_REG_CFG:
        s->cfg = (uint32_t)value;
        break;

    case UART_REG_FIFO: {
        uint8_t ch = (uint8_t)(value & 0xffu);
        s->last_tx = ch;

        if (s->pty_fd >= 0) {
            api_pty_write_req(s->pty_fd, ch);
        }
        break;
    }

    default: {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "write to unknown offset 0x%llx (abs=0x%llx) value=0x%llx",
                 (unsigned long long)offset,
                 (unsigned long long)addr,
                 (unsigned long long)value);
        dev_debug(buf);
        break;
    }
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* uart_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_uart, 0, sizeof(g_uart));
    g_uart.pty_fd = api_pty_fd_gen();

    if (g_uart.pty_fd < 0) {
        uart_log("failed to allocate PTY backend");
    } else {
        uart_log("PTY backend created");
    }

    g_uart.poll_timer = qemu_plugin_timer_new_period_ns(uart_poll_rx_cb,
                                                        &g_uart,
                                                        UART_POLL_PERIOD_NS);

    return &g_uart;
}