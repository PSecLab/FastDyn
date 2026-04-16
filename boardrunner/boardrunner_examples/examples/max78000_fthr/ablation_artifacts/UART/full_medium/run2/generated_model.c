// Device Model for UART

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Inferred Register Functions:
// CTRL   (0x00): UART control register, read/write, with one inferred read-only status bit.
// STATUS (0x04): Polled status register. Observed values:
//                0x50  = idle / transmit-ready state
//                0x140 = receive-data-available state
// CLKDIV (0x10): Baud clock divider, read/write
// OSR    (0x14): Oversampling setting, read/write
// FIFO   (0x20): Shared TX/RX data register

#define UART_BASE           0x40042000ULL

#define UART_CTRL_OFF       0x00
#define UART_STATUS_OFF     0x04
#define UART_CLKDIV_OFF     0x10
#define UART_OSR_OFF        0x14
#define UART_FIFO_OFF       0x20

#define UART_CTRL_READONLY_READY_BIT   0x00080000U
#define UART_CTRL_TRIGGER_READY_BIT    0x00008000U

#define UART_STATUS_IDLE_TXREADY       0x00000050U
#define UART_STATUS_RXREADY            0x00000140U

#define UART_RX_FIFO_SIZE 256
#define UART_RX_HOLDOFF_NS 100000ULL

typedef struct UARTState {
    uint32_t ctrl;
    uint32_t clkdiv;
    uint32_t osr;

    int pty_fd;
    uint64_t rx_timer;
    bool rx_timer_armed;
    bool rx_staging_valid;
    uint8_t rx_staging_byte;

    uint8_t rx_fifo[UART_RX_FIFO_SIZE];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;
} UARTState;

static UARTState g_uart;

static uint64_t uart_size_mask(unsigned size)
{
    switch (size) {
    case 1: return 0xFFULL;
    case 2: return 0xFFFFULL;
    case 4: return 0xFFFFFFFFULL;
    case 8: return 0xFFFFFFFFFFFFFFFFULL;
    default: return 0xFFFFFFFFFFFFFFFFULL;
    }
}

static void uart_rx_fifo_push(UARTState *s, uint8_t ch)
{
    if (!s || s->rx_count >= UART_RX_FIFO_SIZE) {
        return;
    }

    s->rx_fifo[s->rx_head] = ch;
    s->rx_head = (s->rx_head + 1U) % UART_RX_FIFO_SIZE;
    s->rx_count++;
}

static void uart_rx_timer_cb(void *opaque)
{
    UARTState *s = (UARTState *)opaque;

    if (!s) {
        return;
    }

    s->rx_timer_armed = false;

    if (!s->rx_staging_valid) {
        return;
    }

    uart_rx_fifo_push(s, s->rx_staging_byte);
    s->rx_staging_valid = false;
}

static void uart_try_schedule_rx(UARTState *s)
{
    uint8_t ch;
    int ret;

    if (!s || s->pty_fd < 0) {
        return;
    }

    /*
     * Hardware STATUS is mostly 0x50 and only briefly becomes 0x140 when
     * a received byte is actually presented. Do not greedily drain the host
     * PTY into the UART FIFO on every STATUS poll; instead, stage at most one
     * byte and make it visible after a short delay so RX-ready auto-transitions
     * instead of sticking high.
     */
    if (s->rx_count > 0 || s->rx_timer_armed || s->rx_staging_valid) {
        return;
    }

    ret = api_pty_read_nonblock(s->pty_fd, &ch);
    if (ret <= 0) {
        return;
    }

    s->rx_staging_byte = ch;
    s->rx_staging_valid = true;
    s->rx_timer_armed = true;
    qemu_plugin_timer_alarm(
        s->rx_timer,
        (uint64_t)qemu_plugin_get_virtual_timer() + UART_RX_HOLDOFF_NS
    );
}

static bool uart_rx_pop(UARTState *s, uint8_t *out)
{
    if (!s || s->rx_count == 0) {
        return false;
    }

    if (out) {
        *out = s->rx_fifo[s->rx_tail];
    }

    s->rx_tail = (s->rx_tail + 1U) % UART_RX_FIFO_SIZE;
    s->rx_count--;
    return true;
}

static uint32_t uart_ctrl_read_value(UARTState *s)
{
    uint32_t val = s->ctrl;

    // Inferred from trace:
    // write 0x28C01 -> read back 0x0A8C01
    // So bit 0x00008000 in the writable control state causes a
    // hardware-added read-only status bit at 0x00080000.
    if (val & UART_CTRL_TRIGGER_READY_BIT) {
        val |= UART_CTRL_READONLY_READY_BIT;
    }

    return val;
}

static uint32_t uart_status_read_value(UARTState *s)
{
    uart_try_schedule_rx(s);

    if (s->rx_count > 0) {
        return UART_STATUS_RXREADY;
    }

    return UART_STATUS_IDLE_TXREADY;
}

static uint32_t uart_reg_read_word(UARTState *s, hwaddr reg_off)
{
    switch (reg_off) {
    case UART_CTRL_OFF:
        return uart_ctrl_read_value(s);

    case UART_STATUS_OFF:
        return uart_status_read_value(s);

    case UART_CLKDIV_OFF:
        return s->clkdiv;

    case UART_OSR_OFF:
        return s->osr;

    case UART_FIFO_OFF: {
        uint8_t ch = 0;
        if (uart_rx_pop(s, &ch)) {
            /*
             * Present received bytes one at a time. After software consumes the
             * current byte, allow the next host byte to be scheduled as a later
             * RX-ready event instead of keeping STATUS permanently at 0x140.
             */
            uart_try_schedule_rx(s);
            return (uint32_t)ch;
        }

        uart_try_schedule_rx(s);
        return 0;
    }

    default:
        return 0;
    }
}

static uint32_t uart_reg_peek_word(UARTState *s, hwaddr reg_off)
{
    // Side-effect-free backing value accessor for partial writes.
    switch (reg_off) {
    case UART_CTRL_OFF:
        return s->ctrl;
    case UART_CLKDIV_OFF:
        return s->clkdiv;
    case UART_OSR_OFF:
        return s->osr;
    default:
        return 0;
    }
}

static void uart_reg_write_word(UARTState *s, hwaddr reg_off, uint32_t value)
{
    switch (reg_off) {
    case UART_CTRL_OFF:
        s->ctrl = value;
        break;

    case UART_CLKDIV_OFF:
        s->clkdiv = value;
        break;

    case UART_OSR_OFF:
        s->osr = value;
        break;

    case UART_STATUS_OFF:
        // No status writes observed; ignore.
        break;

    case UART_FIFO_OFF: {
        uint8_t ch = (uint8_t)(value & 0xFFU);
        if (s->pty_fd >= 0) {
            api_pty_write_req(s->pty_fd, ch);
        }
        break;
    }

    default:
        // Unknown/unimplemented register: ignore.
        break;
    }
}

// This function will emulate all device reads
uint64_t uart_read(void *opaque, hwaddr addr, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset;
    hwaddr reg_off;
    unsigned shift;
    uint32_t word_val;
    uint64_t mask;

    if (!s) {
        s = &g_uart;
    }

    if (addr < UART_BASE) {
        return 0;
    }

    offset = addr - UART_BASE;
    reg_off = offset & ~0x3ULL;
    shift = (unsigned)((offset & 0x3ULL) * 8U);

    word_val = uart_reg_read_word(s, reg_off);

    if (size >= 4) {
        return word_val;
    }

    mask = uart_size_mask(size);
    return (word_val >> shift) & mask;
}

// This function will emulate all device writes
void uart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    UARTState *s = (UARTState *)opaque;
    hwaddr offset;
    hwaddr reg_off;
    unsigned shift;
    uint32_t new_word;
    uint32_t old_word;
    uint32_t write_mask;

    if (!s) {
        s = &g_uart;
    }

    if (addr < UART_BASE) {
        return;
    }

    offset = addr - UART_BASE;
    reg_off = offset & ~0x3ULL;
    shift = (unsigned)((offset & 0x3ULL) * 8U);

    if (reg_off == UART_FIFO_OFF) {
        // FIFO semantics are byte-oriented; transmit low byte.
        uart_reg_write_word(s, reg_off, (uint32_t)(value & 0xFFU));
        return;
    }

    if (size >= 4) {
        uart_reg_write_word(s, reg_off, (uint32_t)value);
        return;
    }

    old_word = uart_reg_peek_word(s, reg_off);
    write_mask = (uint32_t)(uart_size_mask(size) << shift);
    new_word = (old_word & ~write_mask) | (((uint32_t)value << shift) & write_mask);

    uart_reg_write_word(s, reg_off, new_word);
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* uart_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_uart, 0, sizeof(g_uart));

    g_uart.pty_fd = api_pty_fd_gen();
    g_uart.rx_timer = qemu_plugin_timer_new_ns(uart_rx_timer_cb, &g_uart);

    return &g_uart;
}