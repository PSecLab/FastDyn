// Device Model for UART
//
// Inferred Register Functions:
//   CTRL   @ 0x00 : UART control/config register. Readback is stateful.
//                   When bit 0x8000 is set, readback also shows 0x80000.
//   STATUS @ 0x04 : Polled status register.
//                   Idle: 0x50  = TX ready + RX empty
//                   RX ready: 0x140 = TX ready + RX data available
//   CLKDIV @ 0x10 : Baud divider register
//   OSR    @ 0x14 : Oversampling register
//   FIFO   @ 0x20 : TX write / RX read data register
//
// This model is intentionally minimal and stateful, matching only the behavior
// evidenced by the supplied trace.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define UART_BASE            0x40042000ULL

#define UART_REG_CTRL        0x00
#define UART_REG_STATUS      0x04
#define UART_REG_CLKDIV      0x10
#define UART_REG_OSR         0x14
#define UART_REG_FIFO        0x20

/* Inferred CTRL bits from trace */
#define UART_CTRL_BAUD_EN    0x00008000u
#define UART_CTRL_READY_RO   0x00080000u

/* Inferred STATUS bits from trace */
#define UART_STATUS_RX_EMPTY 0x00000010u
#define UART_STATUS_TX_READY 0x00000040u
#define UART_STATUS_RX_READY 0x00000100u

#define UART_RX_FIFO_SIZE    256
#define UART_POLL_PERIOD_NS  1000000ULL  /* 1 ms */

typedef struct UARTState {
    uint32_t ctrl;
    uint32_t clkdiv;
    uint32_t osr;

    int pty_fd;
    uint64_t poll_timer;

    uint8_t rx_fifo[UART_RX_FIFO_SIZE];
    unsigned rx_head;
    unsigned rx_tail;
    unsigned rx_count;
} UARTState;

static UARTState g_uart;

static void uart_log(const char *msg)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "UART: %s", msg);
    dev_debug(buf);
}

static uint32_t uart_status_value(UARTState *s)
{
    uint32_t v = UART_STATUS_TX_READY;

    if (s->rx_count > 0) {
        v |= UART_STATUS_RX_READY;
    } else {
        v |= UART_STATUS_RX_EMPTY;
    }

    return v;
}

static uint32_t uart_ctrl_readback(UARTState *s)
{
    uint32_t v = s->ctrl & ~UART_CTRL_READY_RO;

    if (s->ctrl & UART_CTRL_BAUD_EN) {
        v |= UART_CTRL_READY_RO;
    }

    return v;
}

static void uart_rx_push(UARTState *s, uint8_t ch)
{
    if (s->rx_count >= UART_RX_FIFO_SIZE) {
        return;
    }

    s->rx_fifo[s->rx_tail] = ch;
    s->rx_tail = (s->rx_tail + 1) % UART_RX_FIFO_SIZE;
    s->rx_count++;
}

static uint8_t uart_rx_pop(UARTState *s)
{
    uint8_t ch = 0;

    if (s->rx_count == 0) {
        return 0;
    }

    ch = s->rx_fifo[s->rx_head];
    s->rx_head = (s->rx_head + 1) % UART_RX_FIFO_SIZE;
    s->rx_count--;

    return ch;
}

static void uart_poll_pty(void *opaque)
{
    UARTState *s = (UARTState *)opaque;
    uint8_t ch;
    int ret;

    if (!s) {
        return;
    }

    if (s->pty_fd < 0) {
        return;
    }

    while (s->rx_count < UART_RX_FIFO_SIZE) {
        ret = api_pty_read_nonblock(s->pty_fd, &ch);
        if (ret != 1) {
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

    (void)size;

    switch (offset) {
    case UART_REG_CTRL:
        return uart_ctrl_readback(s);

    case UART_REG_STATUS:
        return uart_status_value(s);

    case UART_REG_CLKDIV:
        return s->clkdiv;

    case UART_REG_OSR:
        return s->osr;

    case UART_REG_FIFO:
        return (uint64_t)uart_rx_pop(s);

    default:
        return 0;
    }
}

// This function will emulate all device writes
void uart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    uint32_t v = (uint32_t)value;

    (void)size;

    switch (offset) {
    case UART_REG_CTRL:
        /* Preserve only software-visible writable state; synthesize RO bit on read. */
        s->ctrl = v & ~UART_CTRL_READY_RO;
        break;

    case UART_REG_CLKDIV:
        s->clkdiv = v;
        break;

    case UART_REG_OSR:
        s->osr = v;
        break;

    case UART_REG_FIFO:
        /* Transmit low byte to host PTY. */
        if (s->pty_fd >= 0) {
            api_pty_write_req(s->pty_fd, (uint8_t)(v & 0xFF));
        }
        break;

    case UART_REG_STATUS:
        /* No write behavior evidenced in trace. Ignore. */
        break;

    default:
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* uart_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_uart, 0, sizeof(g_uart));

    g_uart.pty_fd = api_pty_fd_gen();
    if (g_uart.pty_fd < 0) {
        uart_log("failed to create PTY backend");
    }

    g_uart.poll_timer = qemu_plugin_timer_new_period_ns(uart_poll_pty, &g_uart,
                                                        UART_POLL_PERIOD_NS);

    uart_log("initialized");
    return &g_uart;
}