// Device Model for USART1 (STM32F103xx)
// - No trace replay / no enforced loop ordering.
// - RXNE set only when PTY provides a byte; cleared when DR is read.
// - ORE set only if additional PTY data arrives while RXNE still set; cleared by SR read then DR read.
// - TXE/TC modeled for forward progress when UE+TE enabled.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define USART1_BASE      0x40013800ULL

#define USART_SR_OFF     0x00
#define USART_DR_OFF     0x04
#define USART_BRR_OFF    0x08
#define USART_CR1_OFF    0x0C
#define USART_CR2_OFF    0x10
#define USART_CR3_OFF    0x14
#define USART_GTPR_OFF   0x18

// SR bits (subset)
#define SR_ORE   (1u << 3)
#define SR_RXNE  (1u << 5)
#define SR_TC    (1u << 6)
#define SR_TXE   (1u << 7)

// CR1 bits (subset)
#define CR1_RE   (1u << 2)
#define CR1_TE   (1u << 3)
#define CR1_UE   (1u << 13)

typedef struct usart1_state {
    uint32_t SR;
    uint32_t DR;
    uint32_t BRR;
    uint32_t CR1;
    uint32_t CR2;
    uint32_t CR3;
    uint32_t GTPR;

    // Single-byte RX latch (matches STM32F1 “1-byte deep” behavior)
    bool     rx_valid;
    uint8_t  rx_byte;

    // For ORE clear sequencing: ORE cleared by read SR then read DR.
    bool     sr_read_since_last_dr;

    // Host endpoint
    int      pty_fd;
} usart1_state_t;

static usart1_state_t g_usart1;

static void dbg(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

static inline uint32_t usart1_off(hwaddr addr) {
    if (addr >= USART1_BASE && addr < (USART1_BASE + 0x1000)) {
        return (uint32_t)(addr - USART1_BASE);
    }
    // If boardrunner passes an offset already
    return (uint32_t)addr;
}

static inline uint64_t mask_by_size(uint64_t v, unsigned size) {
    switch (size) {
        case 1: return v & 0xFFu;
        case 2: return v & 0xFFFFu;
        default: return v & 0xFFFFFFFFu;
    }
}

static inline bool usart_enabled(const usart1_state_t *s) {
    return (s->CR1 & CR1_UE) != 0;
}
static inline bool rx_enabled(const usart1_state_t *s) {
    return usart_enabled(s) && ((s->CR1 & CR1_RE) != 0);
}
static inline bool tx_enabled(const usart1_state_t *s) {
    return usart_enabled(s) && ((s->CR1 & CR1_TE) != 0);
}

// Poll PTY input non-blocking and update RXNE/ORE semantics.
// - If RX latch empty and a byte arrives: latch it, set RXNE.
// - If RX latch already full and more bytes arrive: set ORE, drop extras.
static void usart1_poll_rx(usart1_state_t *s) {
    if (s->pty_fd < 0) return;

    // Drain a bounded amount to avoid spending too long in one MMIO read
    for (int i = 0; i < 64; i++) {
        uint8_t b = 0;
        int st = api_pty_read_nonblock(s->pty_fd, &b);
        if (st <= 0) break;

        if (!rx_enabled(s)) {
            // Receiver disabled: drop incoming data
            continue;
        }

        if (!s->rx_valid) {
            s->rx_valid = true;
            s->rx_byte  = b;
            s->SR |= SR_RXNE;
        } else {
            // Overrun: new byte arrived before software read DR
            s->SR |= SR_ORE;
            // Drop the extra byte (STM32F1 overrun behavior)
        }
    }

    // Keep RXNE consistent
    if (rx_enabled(s) && s->rx_valid) s->SR |= SR_RXNE;
    else                              s->SR &= ~SR_RXNE;
}

static void usart1_update_tx_flags(usart1_state_t *s) {
    if (!tx_enabled(s)) {
        s->SR &= ~(SR_TXE | SR_TC);
        return;
    }
    // For robustness in polling firmware: always ready.
    // (TXE/TC will be observed as 1 in traces like 0xC0.)
    s->SR |= (SR_TXE | SR_TC);
}

// This function will emulation all device reads
uint64_t usart1_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque;
    usart1_state_t *s = &g_usart1;

    uint32_t off = usart1_off(addr);
    uint32_t r = 0;

    switch (off) {
        case USART_SR_OFF:
            s->sr_read_since_last_dr = true;

            // Make SR reflect real host input availability
            usart1_poll_rx(s);
            usart1_update_tx_flags(s);

            r = s->SR;
            break;

        case USART_DR_OFF:
            if (!usart_enabled(s)) {
                r = 0;
                break;
            }

            // Reading DR returns the latched RX byte if present
            if (rx_enabled(s) && s->rx_valid) {
                r = (uint32_t)s->rx_byte;
                s->rx_valid = false;
            } else {
                r = 0;
            }

            // DR read clears RXNE
            s->SR &= ~SR_RXNE;

            // ORE cleared only by SR read then DR read
            if (s->sr_read_since_last_dr) {
                s->SR &= ~SR_ORE;
            }
            s->sr_read_since_last_dr = false;

            s->DR = r;
            break;

        case USART_BRR_OFF:  r = s->BRR; break;
        case USART_CR1_OFF:  r = s->CR1; break;
        case USART_CR2_OFF:  r = s->CR2; break;
        case USART_CR3_OFF:  r = s->CR3; break;
        case USART_GTPR_OFF: r = s->GTPR; break;

        default:
            r = 0;
            break;
    }

    return mask_by_size(r, size);
}

// This function will emulate all device writes
void usart1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque;
    usart1_state_t *s = &g_usart1;

    uint32_t off = usart1_off(addr);
    uint32_t v32 = (uint32_t)mask_by_size(value, size);

    switch (off) {
        case USART_CR1_OFF: {
            uint32_t prev = s->CR1;
            s->CR1 = v32;

            bool prev_ue = (prev & CR1_UE) != 0;
            bool now_ue  = (s->CR1 & CR1_UE) != 0;

            if (!now_ue) {
                // Disable: clear visible state
                s->SR = 0;
                s->rx_valid = false;
                s->sr_read_since_last_dr = false;
            } else if (!prev_ue && now_ue) {
                // Enable: start clean
                s->SR = 0;
                s->rx_valid = false;
                s->sr_read_since_last_dr = false;
            }

            // Recompute flag gating
            usart1_poll_rx(s);
            usart1_update_tx_flags(s);
            break;
        }

        case USART_CR2_OFF: s->CR2 = v32; break;
        case USART_CR3_OFF: s->CR3 = v32; break;
        case USART_BRR_OFF: s->BRR = v32; break;
        case USART_GTPR_OFF: s->GTPR = v32; break;

        case USART_DR_OFF: {
            s->DR = v32;
            s->sr_read_since_last_dr = false;

            if (tx_enabled(s)) {
                uint8_t b = (uint8_t)(v32 & 0xFFu);
                if (s->pty_fd >= 0) {
                    api_pty_write_req(s->pty_fd, b);
                }
                // Keep TXE/TC asserted for polling-style firmware
                usart1_update_tx_flags(s);
            }
            break;
        }

        case USART_SR_OFF:
            // Ignore SR writes in this minimal model
            break;

        default:
            break;
    }
}

void usart1_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_usart1, 0, sizeof(g_usart1));

    g_usart1.pty_fd = api_pty_fd_gen();
    if (g_usart1.pty_fd < 0) {
        dbg("[USART1] api_pty_fd_gen() failed; /tmp/usart1_pty unavailable.\n");
    } else {
        dbg("[USART1] PTY ready at /tmp/usart1_pty (fd=%d)\n", g_usart1.pty_fd);
    }

    // Start disabled until firmware sets CR1. Flags will be computed on reads/writes.
    g_usart1.SR = 0;
    g_usart1.rx_valid = false;
    g_usart1.sr_read_since_last_dr = false;
}
