// Device Model for UART

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Inferred Register Functions:
// 0x00 CTRL    : UART control/configuration
// 0x04 STATUS  : Polling status (TX ready, RX empty / RX available)
// 0x08 INT_EN  : Likely interrupt enable (not used in trace; stored only)
// 0x0C INT_FL  : Likely interrupt flags  (not used in trace; stored only)
// 0x10 CLKDIV  : Baud divider
// 0x14 OSR     : Oversampling ratio
// 0x20 FIFO    : Combined TX/RX data register

#define UART_BASE           0x40042000ULL

#define UART_CTRL_OFF       0x00
#define UART_STATUS_OFF     0x04
#define UART_INT_EN_OFF     0x08
#define UART_INT_FL_OFF     0x0C
#define UART_CLKDIV_OFF     0x10
#define UART_OSR_OFF        0x14
#define UART_FIFO_OFF       0x20

// Status bits inferred directly from trace behavior.
#define UART_STATUS_TX_READY    0x00000040u
#define UART_STATUS_RX_EMPTY    0x00000010u
#define UART_STATUS_RX_AVAIL    0x00000100u

// Control readback behavior inferred from final init sequence:
// write 0x00028c01 -> read 0x000a8c01
#define UART_CTRL_AUTO_READY    0x00080000u
#define UART_CTRL_READY_TRIG    0x00008000u

#define UART_RX_FIFO_SIZE 256

typedef struct UARTState {
    uint32_t ctrl;
    uint32_t int_en;
    uint32_t int_fl;
    uint32_t clkdiv;
    uint32_t osr;

    uint32_t last_data;

    uint8_t rx_fifo[UART_RX_FIFO_SIZE];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;

    int pty_fd;
} UARTState;

static UARTState g_uart;

static uint64_t uart_mask_by_size(uint64_t value, unsigned size) {
    switch (size) {
    case 1:
        return value & 0xffULL;
    case 2:
        return value & 0xffffULL;
    case 4:
    default:
        return value & 0xffffffffULL;
    }
}

static bool uart_rx_push(UARTState *s, uint8_t ch) {
    if (s->rx_count >= UART_RX_FIFO_SIZE) {
        return false;
    }

    s->rx_fifo[s->rx_tail] = ch;
    s->rx_tail = (s->rx_tail + 1U) % UART_RX_FIFO_SIZE;
    s->rx_count++;
    s->last_data = ch;
    return true;
}

static bool uart_rx_pop(UARTState *s, uint8_t *out) {
    if (s->rx_count == 0) {
        return false;
    }

    *out = s->rx_fifo[s->rx_head];
    s->rx_head = (s->rx_head + 1U) % UART_RX_FIFO_SIZE;
    s->rx_count--;
    s->last_data = *out;
    return true;
}

static void uart_poll_backend(UARTState *s) {
    if (s->pty_fd < 0) {
        return;
    }

    // Drain a few bytes per poll so bursty input becomes visible quickly.
    for (int i = 0; i < 16; i++) {
        uint8_t ch = 0;
        int ret = api_pty_read_nonblock(s->pty_fd, &ch);
        if (ret <= 0) {
            break;
        }
        uart_rx_push(s, ch);
    }
}

static uint32_t uart_compute_status(UARTState *s) {
    uart_poll_backend(s);

    // From trace:
    // idle/no RX pending  -> 0x50
    // RX byte available   -> 0x140
    uint32_t status = UART_STATUS_TX_READY;

    if (s->rx_count == 0) {
        status |= UART_STATUS_RX_EMPTY;
    } else {
        status |= UART_STATUS_RX_AVAIL;
    }

    return status;
}

// This function will emulate all device reads
uint64_t uart_read(void *opaque, hwaddr addr, unsigned size) {
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    uint32_t value = 0;

    switch (offset) {
    case UART_CTRL_OFF:
        value = s->ctrl;
        break;

    case UART_STATUS_OFF:
        value = uart_compute_status(s);
        break;

    case UART_INT_EN_OFF:
        value = s->int_en;
        break;

    case UART_INT_FL_OFF:
        value = s->int_fl;
        break;

    case UART_CLKDIV_OFF:
        value = s->clkdiv;
        break;

    case UART_OSR_OFF:
        value = s->osr;
        break;

    case UART_FIFO_OFF: {
        uint8_t ch = 0;
        uart_poll_backend(s);
        if (uart_rx_pop(s, &ch)) {
            value = ch;
        } else {
            value = s->last_data & 0xffu;
        }
        break;
    }

    default:
        value = 0;
        break;
    }

    return uart_mask_by_size(value, size);
}

// This function will emulate all device writes
void uart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    uint32_t v = (uint32_t)uart_mask_by_size(value, size);

    (void)size;

    switch (offset) {
    case UART_CTRL_OFF:
        s->ctrl = v;

        // Trace-backed behavior: after the final control write with bit 0x8000
        // set, hardware readback adds bit 0x80000.
        if (s->ctrl & UART_CTRL_READY_TRIG) {
            s->ctrl |= UART_CTRL_AUTO_READY;
        } else {
            s->ctrl &= ~UART_CTRL_AUTO_READY;
        }
        break;

    case UART_INT_EN_OFF:
        s->int_en = v;
        break;

    case UART_INT_FL_OFF:
        s->int_fl = v;
        break;

    case UART_CLKDIV_OFF:
        s->clkdiv = v;
        break;

    case UART_OSR_OFF:
        s->osr = v;
        break;

    case UART_FIFO_OFF: {
        uint8_t ch = (uint8_t)(v & 0xffu);
        s->last_data = ch;

        // TX path appears immediately ready in the trace, so transmit directly.
        if (s->pty_fd >= 0) {
            api_pty_write_req(s->pty_fd, ch);
        }
        break;
    }

    default:
        // Ignore unknown writes.
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* uart_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_uart, 0, sizeof(g_uart));
    g_uart.pty_fd = api_pty_fd_gen();

    {
        char msg[] = "UART model initialized\n";
        dev_debug(msg);
    }

    return &g_uart;
}