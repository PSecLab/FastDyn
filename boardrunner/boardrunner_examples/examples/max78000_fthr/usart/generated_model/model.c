// Device Model for UART (MAX78xxx-style, e.g., MAX78000/MAX78002 family)
//
// Implements MMIO for a UART at base 0x40042000 with:
//   CTRL   0x00
//   STATUS 0x04
//   INT_EN 0x08
//   INT_FL 0x0C
//   CLKDIV 0x10
//   OSR    0x14
//   FIFO   0x20
//
// Key behavior for your trace + polling demo:
//   - STATUS returns 0x50 when idle (RX empty + TX empty).
//   - When host types, bytes appear in an emulated RX FIFO:
//       * STATUS.RX_EM clears
//       * STATUS.RX_LVL reflects count (saturating to 0xF)
//       * INT_FL.RX_THD sets when RX non-empty (simple threshold model)
//   - Writes to FIFO transmit to host PTY (/tmp/usart1_pty).
//   - CTRL.BCLKRDY is synthesized when CTRL.BCLKEN is set.
//   - CTRL.TX_FLUSH / CTRL.RX_FLUSH are treated as action bits and auto-cleared.
//
// Build assumptions:
//   - <device.h> and <boardrunner/vio.h> provide hwaddr, ConfigSection, and the APIs listed.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <device.h>
#include <boardrunner/vio.h>

#define UART_BASE        0x40042000ULL

#define UART_OFF_CTRL    0x00
#define UART_OFF_STATUS  0x04
#define UART_OFF_INT_EN  0x08
#define UART_OFF_INT_FL  0x0C
#define UART_OFF_CLKDIV  0x10
#define UART_OFF_OSR     0x14
#define UART_OFF_FIFO    0x20

// CTRL bits (from MAX78xxx-style definitions)
#define UART_CTRL_RX_THD_VAL_MASK   0x0000000FULL  // bits 0..3
#define UART_CTRL_TX_FLUSH          0x00000100UL    // bit 8
#define UART_CTRL_RX_FLUSH          0x00000200UL    // bit 9
#define UART_CTRL_CHAR_SIZE_MASK    0x00000C00UL    // bits 10..11
#define UART_CTRL_BCLKEN            0x00008000UL    // bit 15
#define UART_CTRL_BCLKSRC_MASK      0x00030000UL    // bits 16..17
#define UART_CTRL_BCLKRDY           0x00080000UL    // bit 19

// STATUS bits
#define UART_STATUS_RX_EM           0x00000010UL    // bit 4
#define UART_STATUS_RX_FULL         0x00000020UL    // bit 5
#define UART_STATUS_TX_EM           0x00000040UL    // bit 6
#define UART_STATUS_TX_FULL         0x00000080UL    // bit 7
#define UART_STATUS_RX_LVL_MASK     0x00000F00UL    // bits 8..11
#define UART_STATUS_TX_LVL_MASK     0x0000F000UL    // bits 12..15

// INT_FL / INT_EN bits
#define UART_INT_RX_OV              0x00000008UL    // bit 3
#define UART_INT_RX_THD             0x00000010UL    // bit 4

// FIFO field
#define UART_FIFO_DATA_MASK         0x000000FFUL
#define UART_FIFO_RX_PAR            0x00000100UL    // bit 8

// Emulated FIFO depth (hardware is small; 16/32 are fine for polling demos)
#define UART_RX_DEPTH               32

typedef struct {
    // "Registers"
    uint32_t ctrl;
    uint32_t int_en;
    uint32_t int_fl;
    uint32_t clkdiv;
    uint32_t osr;

    // Backend
    int pty_fd;

    // RX FIFO
    uint8_t  rx_buf[UART_RX_DEPTH];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;

    // Timer to poll host input
    uint64_t poll_timer;
    bool     inited;

    // Debug throttle
    uint32_t dbg_drop_count;
} uart_state_t;

static uart_state_t g_uart;

static void uart_dbg(const char *msg)
{
    // dev_debug takes char*
    dev_debug((char *)msg);
}

static inline uint32_t uart_rx_threshold(const uart_state_t *s)
{
    return (s->ctrl & UART_CTRL_RX_THD_VAL_MASK);
}

static void uart_update_status_and_flags(uart_state_t *s)
{
    // Base "idle" state: RX empty + TX empty, no busy, no levels.
    uint32_t status = (UART_STATUS_RX_EM | UART_STATUS_TX_EM);

    if (s->rx_count > 0) {
        status &= ~UART_STATUS_RX_EM;
        uint32_t lvl = (s->rx_count > 0xF) ? 0xF : s->rx_count;
        status |= (lvl << 8) & UART_STATUS_RX_LVL_MASK;
    }

    if (s->rx_count >= UART_RX_DEPTH) {
        status |= UART_STATUS_RX_FULL;
    }

    // TX is modeled as always-ready (TX empty, not full, level 0).
    status &= ~UART_STATUS_TX_FULL;
    status &= ~UART_STATUS_TX_LVL_MASK;

    // Update RX threshold flag in INT_FL (simple: set when RX non-empty and above threshold)
    // The hardware threshold semantics are "RX level >= RX_THD" (exact details vary).
    // For the polling demo, "non-empty sets RX_THD" works well.
    uint32_t thd = uart_rx_threshold(s);
    if (s->rx_count > 0 && s->rx_count >= (thd ? thd : 1)) {
        s->int_fl |= UART_INT_RX_THD;
    } else {
        s->int_fl &= ~UART_INT_RX_THD;
    }

    // STATUS is not stored in a register; computed on demand.
    (void)status;
}

static bool uart_rx_push(uart_state_t *s, uint8_t b)
{
    if (s->rx_count >= UART_RX_DEPTH) {
        // Overflow: keep last data dropped, set RX_OV
        s->int_fl |= UART_INT_RX_OV;
        s->dbg_drop_count++;
        return false;
    }
    s->rx_buf[s->rx_tail] = b;
    s->rx_tail = (s->rx_tail + 1) % UART_RX_DEPTH;
    s->rx_count++;
    return true;
}

static bool uart_rx_pop(uart_state_t *s, uint8_t *out)
{
    if (s->rx_count == 0) return false;
    *out = s->rx_buf[s->rx_head];
    s->rx_head = (s->rx_head + 1) % UART_RX_DEPTH;
    s->rx_count--;
    return true;
}

static void uart_poll_host_rx(uart_state_t *s)
{
    if (!s->inited || s->pty_fd < 0) return;

    // Drain as many bytes as available (non-blocking 1 byte per call API).
    for (;;) {
        uint8_t b = 0;
        int rc = api_pty_read_nonblock(s->pty_fd, &b);
        if (rc <= 0) break;
        (void)uart_rx_push(s, b);
    }

    uart_update_status_and_flags(s);
}

static void uart_poll_timer_cb(void *opaque)
{
    uart_state_t *s = (uart_state_t *)opaque;
    uart_poll_host_rx(s);
}

static inline uint32_t uart_compute_status(uart_state_t *s)
{
    uart_update_status_and_flags(s);

    uint32_t status = (UART_STATUS_RX_EM | UART_STATUS_TX_EM);

    if (s->rx_count > 0) {
        status &= ~UART_STATUS_RX_EM;
        uint32_t lvl = (s->rx_count > 0xF) ? 0xF : s->rx_count;
        status |= (lvl << 8) & UART_STATUS_RX_LVL_MASK;
    } else {
        status |= UART_STATUS_RX_EM;
        status &= ~UART_STATUS_RX_LVL_MASK;
    }

    if (s->rx_count >= UART_RX_DEPTH) status |= UART_STATUS_RX_FULL;
    else status &= ~UART_STATUS_RX_FULL;

    // TX always empty + not full
    status |= UART_STATUS_TX_EM;
    status &= ~UART_STATUS_TX_FULL;
    status &= ~UART_STATUS_TX_LVL_MASK;

    return status;
}

static inline uint32_t uart_read_ctrl_with_ready(uart_state_t *s)
{
    uint32_t v = s->ctrl;

    // Synthesize BCLKRDY when BCLKEN is set (matches your trace where reads gain 0x80000).
    if (v & UART_CTRL_BCLKEN) v |= UART_CTRL_BCLKRDY;
    else v &= ~UART_CTRL_BCLKRDY;

    // Flush bits are action bits; ensure they read as 0 once processed.
    v &= ~(UART_CTRL_TX_FLUSH | UART_CTRL_RX_FLUSH);

    return v;
}

static inline uint32_t uart_off_from_addr(hwaddr addr)
{
    // Be robust to either "absolute addr" or "offset" being passed in.
    if ((uint64_t)addr >= UART_BASE && (uint64_t)addr < (UART_BASE + 0x1000ULL)) {
        return (uint32_t)((uint64_t)addr - UART_BASE);
    }
    return (uint32_t)addr;
}

// This function will emulate all device reads
uint64_t uart_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    uart_state_t *s = &g_uart;

    uint32_t off = uart_off_from_addr(addr);

    // Poll host RX opportunistically on reads (important for tight polling loops).
    if (off == UART_OFF_STATUS || off == UART_OFF_FIFO || off == UART_OFF_INT_FL) {
        uart_poll_host_rx(s);
    }

    uint32_t ret32 = 0;

    switch (off) {
    case UART_OFF_CTRL:
        ret32 = uart_read_ctrl_with_ready(s);
        break;

    case UART_OFF_STATUS:
        ret32 = uart_compute_status(s);
        break;

    case UART_OFF_INT_EN:
        ret32 = s->int_en;
        break;

    case UART_OFF_INT_FL:
        // Return current flags (W1C handled in write).
        ret32 = s->int_fl;
        break;

    case UART_OFF_CLKDIV:
        ret32 = s->clkdiv;
        break;

    case UART_OFF_OSR:
        ret32 = s->osr;
        break;

    case UART_OFF_FIFO: {
        uint8_t b = 0;
        if (uart_rx_pop(s, &b)) {
            // Data in bits [7:0]. Parity error bit not modeled -> 0.
            ret32 = (uint32_t)b;
        } else {
            ret32 = 0; // If read while empty (shouldn't happen if firmware checks STATUS)
        }
        // Recompute flags after pop.
        uart_update_status_and_flags(s);
        break;
    }

    default:
        // Unknown/unused register
        // Keep quiet to avoid log spam; you can enable if debugging:
        // uart_dbg("[uart] read from unknown offset\n");
        ret32 = 0;
        break;
    }

    // Respect access size
    if (size == 1) return (uint8_t)(ret32 & 0xFF);
    if (size == 2) return (uint16_t)(ret32 & 0xFFFF);
    return (uint64_t)ret32;
}

// This function will emulate all device writes
void uart_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    (void)opaque;
    uart_state_t *s = &g_uart;

    uint32_t off = uart_off_from_addr(addr);

    // Mask value to access size
    uint64_t v = value;
    if (size == 1) v &= 0xFF;
    else if (size == 2) v &= 0xFFFF;
    else v &= 0xFFFFFFFFULL;

    switch (off) {
    case UART_OFF_CTRL: {
        uint32_t new_ctrl = (uint32_t)v;

        // Apply flush actions (and auto-clear them).
        if (new_ctrl & UART_CTRL_RX_FLUSH) {
            s->rx_head = s->rx_tail = s->rx_count = 0;
            s->int_fl &= ~(UART_INT_RX_THD | UART_INT_RX_OV);
        }
        // TX_FLUSH: nothing buffered in this model.

        // Store CTRL with flush bits cleared (action bits).
        new_ctrl &= ~(UART_CTRL_TX_FLUSH | UART_CTRL_RX_FLUSH);

        // Keep BCLKRDY synthesized on reads only.
        new_ctrl &= ~UART_CTRL_BCLKRDY;

        s->ctrl = new_ctrl;

        uart_update_status_and_flags(s);
        break;
    }

    case UART_OFF_INT_EN:
        s->int_en = (uint32_t)v;
        break;

    case UART_OFF_INT_FL:
        // Write-1-to-clear
        s->int_fl &= ~((uint32_t)v);
        uart_update_status_and_flags(s);
        break;

    case UART_OFF_CLKDIV:
        s->clkdiv = (uint32_t)v;
        break;

    case UART_OFF_OSR:
        s->osr = (uint32_t)v;
        break;

    case UART_OFF_FIFO: {
        // TX: send low byte to host PTY
        uint8_t b = (uint8_t)(v & 0xFF);
        if (s->pty_fd >= 0) {
            api_pty_write_req(s->pty_fd, b);
        }
        // STATUS stays TX empty in this simplified model.
        break;
    }

    default:
        // Unknown/unused register
        // uart_dbg("[uart] write to unknown offset\n");
        break;
    }
}

void uart_init(ConfigSection* model_info)
{
    (void)model_info;
    memset(&g_uart, 0, sizeof(g_uart));
    g_uart.pty_fd = -1;

    // Default "idle" like your hardware STATUS=0x50.
    // (Computed dynamically, but keep state clean.)
    g_uart.ctrl   = 0;
    g_uart.int_en = 0;
    g_uart.int_fl = 0;
    g_uart.clkdiv = 0;
    g_uart.osr    = 0;

    // Create/open PTY backend at fixed path /tmp/usart1_pty
    g_uart.pty_fd = api_pty_fd_gen();
    g_uart.inited = true;

    // Periodic poll to ingest host keystrokes even if firmware isn't hammering STATUS.
    // 1ms period is usually plenty for console I/O.
    g_uart.poll_timer = qemu_plugin_timer_new_period_ns(uart_poll_timer_cb, &g_uart, 1000ULL * 1000ULL);

    uart_dbg("[uart] init complete (PTY at /tmp/usart1_pty)\n");
}
