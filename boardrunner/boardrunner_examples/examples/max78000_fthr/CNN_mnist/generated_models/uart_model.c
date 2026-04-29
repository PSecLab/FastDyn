// Device Model for UART

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Inferred Register Functions:
// CTRL   (0x00): UART control/configuration, read-modify-write, with one inferred HW-ready bit.
// STATUS (0x04): Polled status. Observed values imply TX-ready always set and RX-empty/RX-available toggle.
// CLKDIV (0x10): Baud/config divider register.
// OSR    (0x14): Oversampling configuration register.
// FIFO   (0x20): TX write / RX read data register.

#define UART_BASE           0x40042000ULL

#define UART_CTRL_OFFSET    0x00
#define UART_STATUS_OFFSET  0x04
#define UART_CLKDIV_OFFSET  0x10
#define UART_OSR_OFFSET     0x14
#define UART_FIFO_OFFSET    0x20

/* Inferred CTRL behavior from trace:
 * write 0x28C01 -> read back 0xA8C01, i.e. bit 0x80000 becomes set by hardware.
 * We model that as a derived ready bit once the 0x8000 configuration bit is present.
 */
#define UART_CTRL_CFG_ACTIVE    0x00008000u
#define UART_CTRL_HW_READY      0x00080000u

/* Inferred STATUS values from trace:
 * idle/no RX data: 0x50
 * RX data available: 0x140
 */
#define UART_STATUS_TX_READY    0x00000040u
#define UART_STATUS_RX_EMPTY    0x00000010u
#define UART_STATUS_RX_AVAIL    0x00000100u

#define UART_RX_FIFO_CAPACITY   256
#define UART_RX_POLL_PERIOD_NS  1000000ULL  /* 1 ms */

typedef struct UARTState {
    uint32_t ctrl;
    uint32_t clkdiv;
    uint32_t osr;

    uint8_t rx_fifo[UART_RX_FIFO_CAPACITY];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;

    int pty_fd;
    uint64_t rx_poll_timer;
} UARTState;

static UARTState g_uart;

static uint32_t uart_get_reg_part(uint32_t reg, hwaddr addr, unsigned size)
{
    unsigned byte_shift = (unsigned)(addr & 0x3ULL) * 8;

    if (size >= 4) {
        return reg;
    }

    if (size == 1) {
        return (reg >> byte_shift) & 0xFFu;
    }

    if (size == 2) {
        return (reg >> byte_shift) & 0xFFFFu;
    }

    /* Fallback for unexpected sizes */
    return reg;
}

static uint32_t uart_set_reg_part(uint32_t old_reg, hwaddr addr, uint64_t value, unsigned size)
{
    unsigned byte_shift = (unsigned)(addr & 0x3ULL) * 8;
    uint32_t mask;

    if (size >= 4) {
        return (uint32_t)value;
    }

    if (size == 1) {
        mask = 0xFFu << byte_shift;
    } else if (size == 2) {
        mask = 0xFFFFu << byte_shift;
    } else {
        return (uint32_t)value;
    }

    return (old_reg & ~mask) | ((((uint32_t)value) << byte_shift) & mask);
}

static bool uart_rx_push(UARTState *s, uint8_t ch)
{
    if (s->rx_count >= UART_RX_FIFO_CAPACITY) {
        return false;
    }

    s->rx_fifo[s->rx_tail] = ch;
    s->rx_tail = (s->rx_tail + 1) % UART_RX_FIFO_CAPACITY;
    s->rx_count++;
    return true;
}

static bool uart_rx_pop(UARTState *s, uint8_t *out)
{
    if (s->rx_count == 0) {
        return false;
    }

    *out = s->rx_fifo[s->rx_head];
    s->rx_head = (s->rx_head + 1) % UART_RX_FIFO_CAPACITY;
    s->rx_count--;
    return true;
}

static void uart_poll_host_rx(UARTState *s)
{
    uint8_t ch;
    int ret;

    if (s->pty_fd < 0) {
        return;
    }

    while (s->rx_count < UART_RX_FIFO_CAPACITY) {
        ret = api_pty_read_nonblock(s->pty_fd, &ch);
        if (ret <= 0) {
            break;
        }

        if (!uart_rx_push(s, ch)) {
            break;
        }
    }
}

static uint32_t uart_compute_status(UARTState *s)
{
    uart_poll_host_rx(s);

    if (s->rx_count > 0) {
        return UART_STATUS_TX_READY | UART_STATUS_RX_AVAIL; /* 0x140 */
    }

    return UART_STATUS_TX_READY | UART_STATUS_RX_EMPTY;     /* 0x50 */
}

static void uart_update_ctrl_derived_bits(UARTState *s)
{
    /* Minimal inferred hardware behavior:
     * once the final configuration pattern includes 0x8000,
     * hardware reflects an additional ready/status bit 0x80000 in CTRL.
     */
    if (s->ctrl & UART_CTRL_CFG_ACTIVE) {
        s->ctrl |= UART_CTRL_HW_READY;
    } else {
        s->ctrl &= ~UART_CTRL_HW_READY;
    }
}

static void uart_rx_timer_cb(void *opaque)
{
    UARTState *s = (UARTState *)opaque;
    uart_poll_host_rx(s);
}

static void uart_send_byte(UARTState *s, uint8_t byte)
{
    if (s->pty_fd >= 0) {
        api_pty_write_req(s->pty_fd, byte);
    }
}

uint64_t uart_read(void *opaque, hwaddr addr, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    hwaddr regoff = offset & ~0x3ULL;
    uint32_t regval = 0;
    uint8_t ch = 0;

    switch (regoff) {
    case UART_CTRL_OFFSET:
        regval = s->ctrl;
        return uart_get_reg_part(regval, offset, size);

    case UART_STATUS_OFFSET:
        regval = uart_compute_status(s);
        return uart_get_reg_part(regval, offset, size);

    case UART_CLKDIV_OFFSET:
        regval = s->clkdiv;
        return uart_get_reg_part(regval, offset, size);

    case UART_OSR_OFFSET:
        regval = s->osr;
        return uart_get_reg_part(regval, offset, size);

    case UART_FIFO_OFFSET:
        uart_poll_host_rx(s);
        if (uart_rx_pop(s, &ch)) {
            regval = (uint32_t)ch;
        } else {
            regval = 0;
        }
        return uart_get_reg_part(regval, offset, size);

    default:
        return 0;
    }
}

void uart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    hwaddr regoff = offset & ~0x3ULL;
    uint8_t txb;

    switch (regoff) {
    case UART_CTRL_OFFSET:
        s->ctrl = uart_set_reg_part(s->ctrl, offset, value, size);
        uart_update_ctrl_derived_bits(s);
        break;

    case UART_CLKDIV_OFFSET:
        s->clkdiv = uart_set_reg_part(s->clkdiv, offset, value, size);
        break;

    case UART_OSR_OFFSET:
        s->osr = uart_set_reg_part(s->osr, offset, value, size);
        break;

    case UART_FIFO_OFFSET:
        /* Treat FIFO write as transmit data. */
        txb = (uint8_t)(value & 0xFFu);
        uart_send_byte(s, txb);
        break;

    case UART_STATUS_OFFSET:
        /* No evidence of writable STATUS semantics in the trace. Ignore writes. */
        break;

    default:
        /* Unknown/unimplemented register: ignore. */
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* uart_init(ConfigSection* model_info)
{
    char msg[128];

    (void)model_info;

    memset(&g_uart, 0, sizeof(g_uart));

    g_uart.pty_fd = api_pty_fd_gen();
    if (g_uart.pty_fd < 0) {
        snprintf(msg, sizeof(msg), "uart: failed to create PTY backend");
        dev_debug(msg);
    } else {
        snprintf(msg, sizeof(msg), "uart: PTY backend created (fd=%d)", g_uart.pty_fd);
        dev_debug(msg);
    }

    /* Poll host PTY periodically so STATUS can reflect incoming RX data. */
    g_uart.rx_poll_timer = qemu_plugin_timer_new_period_ns(uart_rx_timer_cb, &g_uart,
                                                           UART_RX_POLL_PERIOD_NS);

    return &g_uart;
}