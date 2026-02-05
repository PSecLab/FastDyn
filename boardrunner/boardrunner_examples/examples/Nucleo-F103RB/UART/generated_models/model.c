

// Device Model for USART2 (STM32F103xx)
// Based ONLY on provided MMIO traces.
// - Polling-driven TX: SR returns TXE|TC (0xC0) so firmware can write DR.
// - Host PTY backend: DR writes go to PTY, PTY reads feed DR reads + RXNE.
//
// Notes:
// - No IRQ number is provided in the trace data. This model does not raise IRQs by default.
//   If you know the correct IRQ line in your environment, define USART2_IRQ >= 0 at compile time
//   and the model will raise it for RXNE events when RXNEIE is set.
//
// Includes required by your environment:
#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#ifndef USART2_IRQ
#define USART2_IRQ (-1)
#endif

// Base + offsets (USART2)
#define USART2_BASE   0x40004400ULL
#define REG_SR        0x00
#define REG_DR        0x04
#define REG_BRR       0x08
#define REG_CR1       0x0C
#define REG_CR2       0x10
#define REG_CR3       0x14
#define REG_GTPR      0x18

// SR bits (subset)
#define SR_RXNE       (1U << 5)
#define SR_TC         (1U << 6)
#define SR_TXE        (1U << 7)

// CR1 bits (subset)
#define CR1_RE        (1U << 2)
#define CR1_TE        (1U << 3)
#define CR1_RXNEIE    (1U << 5)
#define CR1_UE        (1U << 13)

// Simple RX FIFO
#define RX_FIFO_SZ 256

typedef struct {
    // Registers
    uint32_t sr;   // stored base bits (we also synthesize TXE/TC/RXNE dynamically)
    uint32_t dr;   // last DR written / last DR read (for visibility)
    uint32_t brr;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3;
    uint32_t gtpr;

    // Host backend
    int pty_fd;

    // RX fifo
    uint8_t  rx_fifo[RX_FIFO_SZ];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;

    // Timer to poll host for RX bytes
    uint64_t rx_poll_timer;
} usart2_state_t;

static usart2_state_t g_usart2;

static void dbg(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // dev_debug takes char*
    dev_debug(buf);
}

static inline bool fifo_push(usart2_state_t *s, uint8_t b)
{
    if (s->rx_count >= RX_FIFO_SZ) return false;
    s->rx_fifo[s->rx_head] = b;
    s->rx_head = (s->rx_head + 1) % RX_FIFO_SZ;
    s->rx_count++;
    return true;
}

static inline bool fifo_pop(usart2_state_t *s, uint8_t *out)
{
    if (s->rx_count == 0) return false;
    *out = s->rx_fifo[s->rx_tail];
    s->rx_tail = (s->rx_tail + 1) % RX_FIFO_SZ;
    s->rx_count--;
    return true;
}

static inline bool usart_enabled(const usart2_state_t *s)
{
    return (s->cr1 & CR1_UE) != 0;
}
static inline bool rx_enabled(const usart2_state_t *s)
{
    return usart_enabled(s) && ((s->cr1 & CR1_RE) != 0);
}
static inline bool tx_enabled(const usart2_state_t *s)
{
    return usart_enabled(s) && ((s->cr1 & CR1_TE) != 0);
}

static uint32_t synth_sr(usart2_state_t *s)
{
    // Start from stored base bits, then synthesize the observed behavior:
    // - TXE and TC are effectively always 1 in the trace (0xC0).
    // - RXNE is set if fifo has data.
    uint32_t v = s->sr;

    // Clear bits we fully control
    v &= ~(SR_RXNE | SR_TXE | SR_TC);

    if (tx_enabled(s)) {
        v |= (SR_TXE | SR_TC);
    } else {
        // If TX not enabled, we keep them cleared (conservative).
    }

    if (rx_enabled(s) && s->rx_count > 0) {
        v |= SR_RXNE;
    }
    return v;
}

static void maybe_raise_rx_irq(usart2_state_t *s)
{
    if (USART2_IRQ < 0) return;
    if (!rx_enabled(s)) return;
    if ((s->cr1 & CR1_RXNEIE) == 0) return;
    if (s->rx_count == 0) return;

    qemu_plugin_raise_irq(USART2_IRQ, false);
}

// Periodic poll from host PTY -> push into RX FIFO
static void usart2_rx_poll_cb(void *opaque)
{
    usart2_state_t *s = (usart2_state_t *)opaque;
    if (!s) s = &g_usart2;

    if (s->pty_fd < 0) return;
    if (!rx_enabled(s)) return;

    // Drain available bytes (non-blocking), stop if FIFO fills.
    for (;;) {
        uint8_t b = 0;
        int st = api_pty_read_nonblock(s->pty_fd, &b);
        if (st <= 0) break; // 0: no data, <0: error/none

        if (!fifo_push(s, b)) {
            // FIFO full; drop further input for now.
            dbg("[usart2] RX FIFO full, dropping byte\n");
            break;
        }
    }

    // If we now have data, optionally raise RX IRQ.
    if (s->rx_count > 0) {
        maybe_raise_rx_irq(s);
    }
}

// This function will emulate all device reads
uint64_t usart2_read(void *opaque, hwaddr addr, unsigned size)
{
    usart2_state_t *s = (usart2_state_t *)opaque;
    if (!s) s = &g_usart2;

    uint64_t off = (uint64_t)addr - USART2_BASE;
    uint32_t val = 0;

    switch (off) {
    case REG_SR:
        val = synth_sr(s);
        break;

    case REG_DR: {
        // Reading DR returns next received byte if available.
        // If no data, return 0.
        uint8_t b = 0;
        if (rx_enabled(s) && fifo_pop(s, &b)) {
            val = (uint32_t)b;
            s->dr = val;
        } else {
            val = 0;
        }
        break;
    }

    case REG_BRR:  val = s->brr;  break;
    case REG_CR1:  val = s->cr1;  break;
    case REG_CR2:  val = s->cr2;  break;
    case REG_CR3:  val = s->cr3;  break;
    case REG_GTPR: val = s->gtpr; break;

    default:
        // Unknown/unmodeled register: return 0 (safe default).
        val = 0;
        break;
    }

    // Respect access size (common traces use 32-bit)
    if (size == 1) return (uint8_t)val;
    if (size == 2) return (uint16_t)val;
    return (uint32_t)val;
}

// This function will emulate all device writes
void usart2_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    usart2_state_t *s = (usart2_state_t *)opaque;
    if (!s) s = &g_usart2;

    // Normalize value based on access size
    uint32_t v32;
    if (size == 1) v32 = (uint8_t)value;
    else if (size == 2) v32 = (uint16_t)value;
    else v32 = (uint32_t)value;

    uint64_t off = (uint64_t)addr - USART2_BASE;

    switch (off) {
    case REG_SR:
        // Many STM32 flags are cleared by writing 0 or via read sequences.
        // Not observed in trace; store as base bits but do not allow clobbering TXE/TC/RXNE synthesis.
        s->sr = v32;
        break;

    case REG_DR: {
        // Transmit: write low byte to host PTY if enabled.
        s->dr = v32;

        if (tx_enabled(s) && s->pty_fd >= 0) {
            uint8_t ch = (uint8_t)(v32 & 0xFF);
            api_pty_write_req(s->pty_fd, ch);
        }
        // Trace suggests SR stays at 0xC0 in polling loops; our SR synthesis already keeps TXE/TC set.
        break;
    }

    case REG_BRR:
        s->brr = v32;
        break;

    case REG_CR1: {
        uint32_t prev = s->cr1;
        s->cr1 = v32;

        // If UE was cleared, conservatively drop RX FIFO (acts like disabling the peripheral).
        bool prev_ue = (prev & CR1_UE) != 0;
        bool now_ue  = (s->cr1 & CR1_UE) != 0;
        if (prev_ue && !now_ue) {
            s->rx_head = s->rx_tail = s->rx_count = 0;
        }

        // If RXNEIE becomes enabled and data exists, raise an IRQ (if configured)
        if ((s->cr1 & CR1_RXNEIE) && s->rx_count > 0) {
            maybe_raise_rx_irq(s);
        }
        break;
    }

    case REG_CR2:
        s->cr2 = v32;
        break;

    case REG_CR3:
        s->cr3 = v32;
        break;

    case REG_GTPR:
        s->gtpr = v32;
        break;

    default:
        // Ignore unknown writes
        break;
    }
}

void usart2_init(ConfigSection* model_info)
{
    (void)model_info; // not used (no config accessors provided in the allowed API list)

    memset(&g_usart2, 0, sizeof(g_usart2));

    // From trace, SR reads as 0xC0 (TXE|TC). We synthesize these dynamically when TX is enabled.
    g_usart2.sr = 0;

    // Create/open PTY endpoint provided by environment.
    // API description says it returns fd for /tmp/usart1_pty (fixed path in this environment).
    g_usart2.pty_fd = api_pty_fd_gen();
    if (g_usart2.pty_fd < 0) {
        dbg("[usart2] failed to create/open PTY backend\n");
    } else {
        dbg("[usart2] PTY backend opened (see /tmp/usart1_pty)\n");
    }

    // Poll host input periodically (keeps RX usable even though trace didn't show RX reads).
    // 1ms period is a reasonable low-overhead default.
    g_usart2.rx_poll_timer = qemu_plugin_timer_new_period_ns(usart2_rx_poll_cb, &g_usart2, 1000ULL * 1000ULL);

    dbg("[usart2] init done\n");
}
