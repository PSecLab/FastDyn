// Device Model for UART

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define UART_BASE               0x40042000ULL

#define UART_CTRL_OFFSET        0x00
#define UART_STATUS_OFFSET      0x04
#define UART_CLKDIV_OFFSET      0x10
#define UART_OSR_OFFSET         0x14
#define UART_FIFO_OFFSET        0x20

/* Observed STATUS values from the trace */
#define UART_STATUS_IDLE        0x00000050u
#define UART_STATUS_RX_READY    0x00000140u

/*
 * Observed CTRL behavior:
 *   write 0x28C01 -> subsequent read 0xA8C01
 * This suggests a hardware-set bit 0x80000 appears when 0x8000 is written.
 * Names are unknown, so keep them descriptive but generic.
 */
#define UART_CTRL_OBS_TRIGGER_BIT   0x00008000u
#define UART_CTRL_OBS_READY_BIT     0x00080000u

#define UART_RX_FIFO_SIZE       256
#define UART_POLL_PERIOD_NS     1000000ULL   /* 1 ms periodic PTY poll */

typedef struct UARTState {
    uint32_t ctrl;
    uint32_t clkdiv;
    uint32_t osr;

    uint8_t rx_fifo[UART_RX_FIFO_SIZE];
    uint16_t rx_head;
    uint16_t rx_tail;
    uint16_t rx_count;

    int pty_fd;
    uint64_t poll_timer;
} UARTState;

static UARTState g_uart;

static void uart_debug(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

static uint64_t uart_mask_read(uint32_t value, unsigned size)
{
    switch (size) {
    case 1:
        return value & 0xFFu;
    case 2:
        return value & 0xFFFFu;
    case 4:
    default:
        return value;
    }
}

static uint32_t uart_status(UARTState *s)
{
    if (s->rx_count > 0) {
        return UART_STATUS_RX_READY;
    }
    return UART_STATUS_IDLE;
}

static bool uart_rx_push(UARTState *s, uint8_t ch)
{
    if (s->rx_count >= UART_RX_FIFO_SIZE) {
        return false;
    }

    s->rx_fifo[s->rx_tail] = ch;
    s->rx_tail = (uint16_t)((s->rx_tail + 1) % UART_RX_FIFO_SIZE);
    s->rx_count++;
    return true;
}

static bool uart_rx_pop(UARTState *s, uint8_t *out)
{
    if (s->rx_count == 0) {
        return false;
    }

    *out = s->rx_fifo[s->rx_head];
    s->rx_head = (uint16_t)((s->rx_head + 1) % UART_RX_FIFO_SIZE);
    s->rx_count--;
    return true;
}

/* Periodic callback: poll host PTY for incoming bytes */
static void uart_poll_rx(void *opaque)
{
    UARTState *s = (UARTState *)opaque;
    uint8_t ch;
    int ret;

    if (s == NULL || s->pty_fd < 0) {
        return;
    }

    while (s->rx_count < UART_RX_FIFO_SIZE) {
        ret = api_pty_read_nonblock(s->pty_fd, &ch);
        if (ret <= 0) {
            break;
        }
        if (!uart_rx_push(s, ch)) {
            break;
        }
    }
}

/*
 * Inferred Register Functions:
 * 0x00 CTRL   - control/config register, stateful, read-modify-write
 * 0x04 STATUS - polled status, returns 0x50 idle or 0x140 when RX data buffered
 * 0x10 CLKDIV - baud divider/config
 * 0x14 OSR    - oversampling ratio/config
 * 0x20 FIFO   - TX on write, RX on read
 */

// This function will emulate all device reads
uint64_t uart_read(void *opaque, hwaddr addr, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    uint8_t ch;

    switch (offset) {
    case UART_CTRL_OFFSET:
        return uart_mask_read(s->ctrl, size);

    case UART_STATUS_OFFSET:
        return uart_mask_read(uart_status(s), size);

    case UART_CLKDIV_OFFSET:
        return uart_mask_read(s->clkdiv, size);

    case UART_OSR_OFFSET:
        return uart_mask_read(s->osr, size);

    case UART_FIFO_OFFSET:
        if (uart_rx_pop(s, &ch)) {
            return uart_mask_read((uint32_t)ch, size);
        }
        return 0;

    default:
        uart_debug("uart_read: unknown offset 0x%llx (addr=0x%llx, size=%u)",
                   (unsigned long long)offset,
                   (unsigned long long)addr,
                   size);
        return 0;
    }
}

// This function will emulate all device writes
void uart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;

    (void)size;

    switch (offset) {
    case UART_CTRL_OFFSET:
        s->ctrl = (uint32_t)value;

        /* Reproduce the observed hardware-set bit behavior. */
        if (s->ctrl & UART_CTRL_OBS_TRIGGER_BIT) {
            s->ctrl |= UART_CTRL_OBS_READY_BIT;
        }
        break;

    case UART_CLKDIV_OFFSET:
        s->clkdiv = (uint32_t)value;
        break;

    case UART_OSR_OFFSET:
        s->osr = (uint32_t)value;
        break;

    case UART_FIFO_OFFSET: {
        uint8_t ch = (uint8_t)(value & 0xFFu);
        if (s->pty_fd >= 0) {
            api_pty_write_req(s->pty_fd, ch);
        }
        break;
    }

    case UART_STATUS_OFFSET:
        /*
         * No write behavior was observed for STATUS in the trace.
         * Ignore writes safely.
         */
        break;

    default:
        uart_debug("uart_write: unknown offset 0x%llx (addr=0x%llx, value=0x%llx, size=%u)",
                   (unsigned long long)offset,
                   (unsigned long long)addr,
                   (unsigned long long)value,
                   size);
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
        uart_debug("uart_init: failed to allocate PTY backend");
    }

    /*
     * Poll host-side PTY periodically so incoming serial data becomes visible
     * through STATUS/FIFO reads without requiring any missing event API.
     */
    g_uart.poll_timer = qemu_plugin_timer_new_period_ns(uart_poll_rx, &g_uart,
                                                        UART_POLL_PERIOD_NS);

    return &g_uart;
}