// Device Model for UART

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Inferred Register Functions:
//   CTRL   (0x00): UART control/configuration; read-modify-write; gains a read-only ready bit.
//   STATUS (0x04): Polled status; 0x50 when idle, 0x140 when RX data is available.
//   CLKDIV (0x10): Baud clock divider.
//   OSR    (0x14): Oversampling ratio.
//   FIFO   (0x20): Shared TX/RX data register.

#define UART_BASE           0x40042000ULL

#define UART_CTRL_OFF       0x00
#define UART_STATUS_OFF     0x04
#define UART_CLKDIV_OFF     0x10
#define UART_OSR_OFF        0x14
#define UART_FIFO_OFF       0x20

// Bits inferred from trace behavior.
#define UART_CTRL_ENABLE_BIT        0x00000001U
#define UART_CTRL_READY_GATE_BIT    0x00008000U
#define UART_CTRL_READY_BIT         0x00080000U   // Read-only-ish bit seen in final CTRL readback.

#define UART_STATUS_RX_EMPTY_BIT    0x00000010U
#define UART_STATUS_TX_READY_BIT    0x00000040U
#define UART_STATUS_RX_READY_BIT    0x00000100U

#define UART_RX_BUF_SIZE 256
#define UART_POLL_PERIOD_NS 1000000ULL  // 1 ms periodic poll of host PTY.

typedef struct {
    uint32_t ctrl;
    uint32_t clkdiv;
    uint32_t osr;

    uint8_t rx_buf[UART_RX_BUF_SIZE];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;

    int pty_fd;
    uint64_t poll_timer;
} UARTState;

static UARTState g_uart;

static uint64_t uart_mask_by_size(uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        return value & 0xFFU;
    case 2:
        return value & 0xFFFFU;
    case 4:
        return value & 0xFFFFFFFFULL;
    default:
        return value;
    }
}

static bool uart_rx_is_empty(UARTState *s)
{
    return s->rx_count == 0;
}

static bool uart_rx_is_full(UARTState *s)
{
    return s->rx_count >= UART_RX_BUF_SIZE;
}

static void uart_rx_push(UARTState *s, uint8_t value)
{
    if (uart_rx_is_full(s)) {
        dev_debug("UART: RX buffer full, dropping byte");
        return;
    }

    s->rx_buf[s->rx_tail] = value;
    s->rx_tail = (s->rx_tail + 1U) % UART_RX_BUF_SIZE;
    s->rx_count++;
}

static uint8_t uart_rx_pop(UARTState *s)
{
    uint8_t value = 0;

    if (uart_rx_is_empty(s)) {
        return 0;
    }

    value = s->rx_buf[s->rx_head];
    s->rx_head = (s->rx_head + 1U) % UART_RX_BUF_SIZE;
    s->rx_count--;
    return value;
}

static uint32_t uart_ctrl_readback(UARTState *s)
{
    uint32_t v = s->ctrl;

    // Minimal dynamic behavior inferred from init trace:
    // after enabling/configuring UART, an extra bit (0x80000) appears in CTRL reads.
    if ((s->ctrl & UART_CTRL_ENABLE_BIT) &&
        (s->ctrl & UART_CTRL_READY_GATE_BIT) &&
        (s->clkdiv != 0) &&
        (s->osr != 0)) {
        v |= UART_CTRL_READY_BIT;
    }

    return v;
}

static uint32_t uart_status_readback(UARTState *s)
{
    // No evidence of meaningful status while disabled.
    if ((s->ctrl & UART_CTRL_ENABLE_BIT) == 0) {
        return 0;
    }

    // Trace supports:
    //   idle/no RX data -> 0x50  = 0x40 | 0x10
    //   RX data present  -> 0x140 = 0x40 | 0x100
    if (uart_rx_is_empty(s)) {
        return UART_STATUS_TX_READY_BIT | UART_STATUS_RX_EMPTY_BIT;
    } else {
        return UART_STATUS_TX_READY_BIT | UART_STATUS_RX_READY_BIT;
    }
}

static void uart_rx_poll_cb(void *opaque)
{
    UARTState *s = (UARTState *)opaque;
    uint8_t byte = 0;
    int rc = 0;

    if (s->pty_fd < 0) {
        return;
    }

    while (!uart_rx_is_full(s)) {
        rc = api_pty_read_nonblock(s->pty_fd, &byte);
        if (rc <= 0) {
            break;
        }
        uart_rx_push(s, byte);
    }
}

// This function will emulate all device reads
uint64_t uart_read(void *opaque, hwaddr addr, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    uint64_t value = 0;

    switch (offset) {
    case UART_CTRL_OFF:
        value = uart_ctrl_readback(s);
        break;

    case UART_STATUS_OFF:
        value = uart_status_readback(s);
        break;

    case UART_CLKDIV_OFF:
        value = s->clkdiv;
        break;

    case UART_OSR_OFF:
        value = s->osr;
        break;

    case UART_FIFO_OFF:
        value = uart_rx_pop(s);
        break;

    default:
        value = 0;
        break;
    }

    return uart_mask_by_size(value, size);
}

// This function will emulate all device writes
void uart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset = addr - UART_BASE;
    uint32_t v32 = (uint32_t)uart_mask_by_size(value, size);

    (void)size;

    switch (offset) {
    case UART_CTRL_OFF:
        // Preserve CTRL as guest-programmed state, excluding the inferred read-only ready bit.
        s->ctrl = v32 & ~UART_CTRL_READY_BIT;
        break;

    case UART_CLKDIV_OFF:
        s->clkdiv = v32;
        break;

    case UART_OSR_OFF:
        s->osr = v32;
        break;

    case UART_FIFO_OFF: {
        uint8_t ch = (uint8_t)(v32 & 0xFFU);

        // TX path is modeled as always ready once enabled, consistent with observed polling.
        if (s->ctrl & UART_CTRL_ENABLE_BIT) {
            if (s->pty_fd >= 0) {
                api_pty_write_req(s->pty_fd, ch);
            }
        }
        break;
    }

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
        dev_debug("UART: failed to create PTY backend");
    } else {
        dev_debug("UART: PTY backend created");
    }

    // Periodically poll host input and feed the emulated RX FIFO.
    g_uart.poll_timer = qemu_plugin_timer_new_period_ns(uart_rx_poll_cb, &g_uart, UART_POLL_PERIOD_NS);

    return &g_uart;
}