// Device Model for UART

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Inferred Register Functions:
//   0x00 CTRL   : UART control/configuration, includes a read-only ready/active bit (0x80000)
//   0x04 STATUS : Polled status register; 0x50 when TX-ready, 0x140 when RX data is available
//   0x10 CLKDIV : Baud divider
//   0x14 OSR    : Oversampling configuration
//   0x20 FIFO   : TX on write, RX on read

#define UART_BASE_DEFAULT          0x40042000ULL

#define UART_REG_CTRL              0x00
#define UART_REG_STATUS            0x04
#define UART_REG_CLKDIV            0x10
#define UART_REG_OSR               0x14
#define UART_REG_FIFO              0x20

/* Inferred CTRL bits from trace */
#define UART_CTRL_EN               0x00000001u
#define UART_CTRL_ACTIVE_CFG       0x00008000u
#define UART_CTRL_READY_RO         0x00080000u

/* Inferred STATUS values/bits from trace */
#define UART_STATUS_TX_READY       0x00000010u
#define UART_STATUS_SHARED         0x00000040u
#define UART_STATUS_RX_READY       0x00000100u

#define UART_STATUS_IDLE_TX        (UART_STATUS_TX_READY | UART_STATUS_SHARED) /* 0x50 */
#define UART_STATUS_RX_AVAIL       (UART_STATUS_RX_READY | UART_STATUS_SHARED) /* 0x140 */

#define UART_RX_FIFO_SIZE          16
#define UART_RX_INITIAL_DELAY_NS   150000000ULL  /* first async RX byte after enable */
#define UART_RX_INTERBYTE_DELAY_NS 150000000ULL  /* gap between scripted RX bytes */

typedef struct UARTState {
    uint64_t base;

    uint32_t ctrl;
    uint32_t clkdiv;
    uint32_t osr;
    uint8_t tx_last;

    uint8_t rx_fifo[UART_RX_FIFO_SIZE];
    unsigned rx_head;
    unsigned rx_tail;
    unsigned rx_count;

    bool ready_latched;

    uint64_t rx_timer;
    bool rx_timer_armed;
    bool rx_stream_started;
    size_t rx_script_pos;
} UARTState;

static UARTState g_uart;

static const uint8_t g_uart_rx_script[] = { 'H', 'e', 'l', 'l', 'o' };

static void uart_debug(const char *msg) {
    dev_debug((char *)msg);
}

static bool uart_enabled(UARTState *s) {
    return (s->ctrl & UART_CTRL_EN) != 0;
}

static void uart_rx_push(UARTState *s, uint8_t ch) {
    if (s->rx_count >= UART_RX_FIFO_SIZE) {
        return;
    }

    s->rx_fifo[s->rx_tail] = ch;
    s->rx_tail = (s->rx_tail + 1) % UART_RX_FIFO_SIZE;
    s->rx_count++;
}

static uint8_t uart_rx_pop(UARTState *s) {
    uint8_t ch = 0;

    if (s->rx_count == 0) {
        return 0;
    }

    ch = s->rx_fifo[s->rx_head];
    s->rx_head = (s->rx_head + 1) % UART_RX_FIFO_SIZE;
    s->rx_count--;
    return ch;
}

static uint32_t uart_ctrl_read_value(UARTState *s) {
    uint32_t v = s->ctrl;

    if (s->ready_latched) {
        v |= UART_CTRL_READY_RO;
    } else {
        v &= ~UART_CTRL_READY_RO;
    }

    return v;
}

static uint32_t uart_status_value(UARTState *s) {
    if (!uart_enabled(s)) {
        return 0;
    }

    /*
     * STATUS=0x140 corresponds to unread RX data being present. Unlike the
     * previous model, this condition must persist until FIFO is actually read;
     * clearing it on a STATUS read loses the receive event and prevents
     * firmware from reaching the FIFO read path seen on hardware.
     */
    if (s->rx_count != 0) {
        return UART_STATUS_RX_AVAIL;
    }

    return UART_STATUS_IDLE_TX;
}

static void uart_schedule_rx_after(UARTState *s, uint64_t delay_ns) {
    int64_t now;

    if (s == NULL) {
        return;
    }

    if (!uart_enabled(s)) {
        return;
    }

    if (s->rx_timer_armed) {
        return;
    }

    if (s->rx_script_pos >= sizeof(g_uart_rx_script)) {
        return;
    }

    now = qemu_plugin_get_virtual_timer();
    if (now < 0) {
        now = 0;
    }

    s->rx_timer_armed = true;
    qemu_plugin_timer_alarm(s->rx_timer, (uint64_t)now + delay_ns);
}

static void uart_rx_timer_cb(void *opaque) {
    UARTState *s = (UARTState *)opaque;

    if (s == NULL) {
        return;
    }

    s->rx_timer_armed = false;

    if (!uart_enabled(s)) {
        return;
    }

    if (s->rx_script_pos >= sizeof(g_uart_rx_script)) {
        return;
    }

    /*
     * RX arrival is asynchronous to TX. Expose at most one unread scripted byte
     * at a time; if firmware has not consumed the previous byte yet, retry
     * later instead of forcing STATUS to remain RX-ready continuously.
     */
    if (s->rx_count != 0) {
        uart_schedule_rx_after(s, UART_RX_INTERBYTE_DELAY_NS);
        return;
    }

    uart_rx_push(s, g_uart_rx_script[s->rx_script_pos]);
    s->rx_script_pos++;
}

static void uart_start_rx_stream(UARTState *s) {
    if (s == NULL) {
        return;
    }

    if (!uart_enabled(s)) {
        return;
    }

    if (s->rx_stream_started) {
        return;
    }

    s->rx_stream_started = true;
    uart_schedule_rx_after(s, UART_RX_INITIAL_DELAY_NS);
}

static uint32_t uart_read_reg32(UARTState *s, uint64_t offset) {
    uint32_t value;

    switch (offset) {
    case UART_REG_CTRL:
        return uart_ctrl_read_value(s);

    case UART_REG_STATUS:
        return uart_status_value(s);

    case UART_REG_CLKDIV:
        return s->clkdiv;

    case UART_REG_OSR:
        return s->osr;

    case UART_REG_FIFO:
        value = (uint32_t)uart_rx_pop(s);

        if ((s->rx_count == 0) &&
            s->rx_stream_started &&
            (s->rx_script_pos < sizeof(g_uart_rx_script))) {
            uart_schedule_rx_after(s, UART_RX_INTERBYTE_DELAY_NS);
        }

        return value;

    default:
        uart_debug("UART: read from unknown register\n");
        return 0;
    }
}

static void uart_write_reg32(UARTState *s, uint64_t offset, uint32_t value) {
    switch (offset) {
    case UART_REG_CTRL: {
        /*
         * Writable state mirrors firmware writes. The trace shows that after
         * writing 0x28C01, reads return 0xA8C01, so we model 0x80000 as a
         * latched read-only ready/active bit that appears once the UART is
         * brought into its fully active configuration.
         */
        s->ctrl = value & ~UART_CTRL_READY_RO;

        if ((value & UART_CTRL_ACTIVE_CFG) && (value & UART_CTRL_EN)) {
            s->ready_latched = true;
        } else if (!(value & UART_CTRL_EN)) {
            s->ready_latched = false;
            s->rx_head = 0;
            s->rx_tail = 0;
            s->rx_count = 0;
            s->rx_timer_armed = false;
            s->rx_stream_started = false;
            s->rx_script_pos = 0;
        }
        break;
    }

    case UART_REG_CLKDIV:
        s->clkdiv = value;
        break;

    case UART_REG_OSR:
        s->osr = value;
        break;

    case UART_REG_FIFO:
        /*
         * TX writes should not directly force STATUS to 0x140, but runtime TX
         * activity is a better point to begin the scripted asynchronous RX
         * stream than initial enable, which otherwise makes RX-ready appear too
         * early and too persistently.
         */
        s->tx_last = (uint8_t)(value & 0xFF);

        if (!s->rx_stream_started && uart_enabled(s) && s->ready_latched) {
            uart_start_rx_stream(s);
        }
        break;

    case UART_REG_STATUS:
        /*
         * No evidence of writable STATUS semantics in trace.
         */
        break;

    default:
        uart_debug("UART: write to unknown register\n");
        break;
    }
}

// This function will emulate all device reads
uint64_t uart_read(void *opaque, hwaddr addr, unsigned size) {
    UARTState *s = (UARTState *)opaque;
    uint64_t offset;
    uint32_t value;

    if (s == NULL) {
        s = &g_uart;
    }

    offset = addr - s->base;
    value = uart_read_reg32(s, offset);

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

// This function will emulate all device writes
void uart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    UARTState *s = (UARTState *)opaque;
    uint64_t offset;
    uint32_t cur;
    uint32_t newv;

    if (s == NULL) {
        s = &g_uart;
    }

    offset = addr - s->base;

    if (size == 4) {
        uart_write_reg32(s, offset, (uint32_t)value);
        return;
    }

    /*
     * Support partial-width accesses conservatively by merging into the current
     * 32-bit register image where possible.
     */
    cur = uart_read_reg32(s, offset);
    newv = cur;

    if (size == 1) {
        newv = (cur & ~0xFFu) | ((uint32_t)value & 0xFFu);
    } else if (size == 2) {
        newv = (cur & ~0xFFFFu) | ((uint32_t)value & 0xFFFFu);
    } else {
        newv = (uint32_t)value;
    }

    uart_write_reg32(s, offset, newv);
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* uart_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_uart, 0, sizeof(g_uart));

    /*
     * The trace identifies the UART at 0x40042000. If the surrounding framework
     * exposes ConfigSection accessors, they can be wired here, but no such API
     * was provided in the prompt.
     */
    g_uart.base = UART_BASE_DEFAULT;
    g_uart.rx_timer = qemu_plugin_timer_new_ns(uart_rx_timer_cb, &g_uart);

    return &g_uart;
}