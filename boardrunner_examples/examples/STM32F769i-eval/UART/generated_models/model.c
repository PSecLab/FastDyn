// Device Model for USART1
#include <device.h>
#include <devmodels_apis.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ====== Register Offsets (from base 0x40011000) ====== */
#define USART_CR1    0x00
#define USART_CR2    0x04
#define USART_CR3    0x08
#define USART_BRR    0x0C
#define USART_RQR    0x18
#define USART_ISR    0x1C
#define USART_ICR    0x20
#define USART_RDR    0x24
#define USART_TDR    0x28

/* ====== CR1 bits (subset used) ====== */
#define CR1_UE       (1u << 0)   // USART enable
#define CR1_RE       (1u << 2)   // Receiver enable
#define CR1_TE       (1u << 3)   // Transmitter enable
#define CR1_RXNEIE   (1u << 5)   // RX not empty interrupt enable
#define CR1_TCIE     (1u << 6)   // Transmission complete interrupt enable
#define CR1_TXEIE    (1u << 7)   // TX empty interrupt enable

/* ====== ISR bits (subset used) ====== */
#define ISR_IDLE     (1u << 4)   // Idle line detected
#define ISR_RXNE     (1u << 5)   // Read data register not empty
#define ISR_TC       (1u << 6)   // Transmission complete
#define ISR_TXE      (1u << 7)   // Transmit data register empty
#define ISR_TEACK    (1u << 21)  // Transmit enable acknowledge
#define ISR_REACK    (1u << 22)  // Receive enable acknowledge

/* ====== RQR bits (subset) ====== */
#define RQR_RXFRQ    (1u << 3)   // Receive data flush request

/* ====== ICR bits (very small subset we honor) ====== */
#define ICR_TCCF     (1u << 6)   // Clear TC flag (mapping convenience)

/* ====== Platform details ====== */
#define USART1_IRQ_VECTOR  37+16
#define PTY_POLL_PERIOD_NS 10000000ULL  // 10ms

typedef struct {
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3;
    uint32_t brr;
    uint32_t isr;
    uint32_t icr;   // last written ICR value (for visibility)
    uint8_t  rdr;   // last received byte
    uint8_t  tdr;   // last transmitted byte
    int      pty_fd;
    uint64_t pty_poll_timer;
} usart1_state_t;

static usart1_state_t s;

/* ---- Helpers ---- */
static inline void usart1_update_te_re_ack(void) {
    // Acknowledge TE/RE whenever UE & TE/RE are set; otherwise clear them.
    if ((s.cr1 & CR1_UE) && (s.cr1 & CR1_TE)) s.isr |= ISR_TEACK; else s.isr &= ~ISR_TEACK;
    if ((s.cr1 & CR1_UE) && (s.cr1 & CR1_RE)) s.isr |= ISR_REACK; else s.isr &= ~ISR_REACK;
}

static inline void usart1_maybe_raise_irq(void) {
    // Raise if any enabled, pending condition exists.
    bool txe_irq = ((s.cr1 & CR1_TXEIE) && (s.isr & ISR_TXE));
    bool tc_irq  = ((s.cr1 & CR1_TCIE)  && (s.isr & ISR_TC));
    bool rx_irq  = ((s.cr1 & CR1_RXNEIE) && (s.isr & ISR_RXNE));

    if (txe_irq || tc_irq || rx_irq) {
        qemu_plugin_raise_irq(USART1_IRQ_VECTOR);
    }
}

static inline void usart1_set_idle_defaults(void) {
    // When enabled but idle, TXE and TC should be high; IDLE can be seen by FW.
    s.isr |= (ISR_TXE | ISR_TC | ISR_IDLE);
    usart1_update_te_re_ack();
    usart1_maybe_raise_irq();
}

/* ---- PTY RX polling ---- */
static void usart1_pty_poll_cb(void *opaque) {
    (void)opaque;
    if (!(s.cr1 & CR1_UE) || !(s.cr1 & CR1_RE)) {
        // Receiver disabled — nothing to do.
        return;
    }

    uint8_t byte;
    int n = api_pty_read_nonblock(s.pty_fd, &byte);
    if (n > 0) {
        s.rdr = byte;
        s.isr |= ISR_RXNE;     // new data available
        s.isr |= ISR_IDLE;     // line is "idle" between bytes; harmless to keep set
        usart1_maybe_raise_irq();
        dev_debug("USART1: RX byte received from PTY");
    }
}

/* ====== MMIO READ ====== */
// This function will emulation all device reads
uint64_t usart1_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque;
    (void)size;
    uint32_t off = (uint32_t)(addr & 0xFFF);
    uint32_t val = 0;

    switch (off) {
    case USART_CR1:  val = s.cr1; break;
    case USART_CR2:  val = s.cr2; break;
    case USART_CR3:  val = s.cr3; break;
    case USART_BRR:  val = s.brr; break;
    case USART_ISR:
        // Ensure TEACK/REACK reflect current TE/RE state at read time
        usart1_update_te_re_ack();
        val = s.isr;
        break;
    case USART_RDR:
        // Reading RDR returns byte and clears RXNE
        val = s.rdr;
        s.isr &= ~ISR_RXNE;
        // Reading RDR may also deassert RXNE-related IRQ
        usart1_maybe_raise_irq();
        break;
    case USART_TDR:
        // Rarely read by FW; return last written value
        val = s.tdr;
        break;
    case USART_RQR:
        // Readback not generally used; return 0
        val = 0;
        break;
    case USART_ICR:
        // Not readable in HW; return 0 for safety
        val = 0;
        break;
    default:
        dev_debug("USART1: Unhandled READ");
        val = 0;
        break;
    }
    return val;
}

/* ====== MMIO WRITE ====== */
// This function will emulate all device writes
void usart1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque;
    (void)size;
    uint32_t off = (uint32_t)(addr & 0xFFF);
    uint32_t v   = (uint32_t)value;

    switch (off) {
    case USART_CR1:
        s.cr1 = v;

        // If UE cleared, drop acknowledges and default status
        if (!(s.cr1 & CR1_UE)) {
            s.isr &= ~(ISR_TEACK | ISR_REACK);
        } else {
            usart1_update_te_re_ack();
            // When enabling TX, make TXE/TC visible immediately (idle transmitter)
            if (s.cr1 & CR1_TE) s.isr |= (ISR_TXE | ISR_TC);
            // Receiver enable doesn't set RXNE; it will be set on next received byte
        }
        usart1_maybe_raise_irq();
        break;

    case USART_CR2:
        s.cr2 = v;
        break;

    case USART_CR3:
        s.cr3 = v;
        break;

    case USART_BRR:
        s.brr = v;
        break;

    case USART_RQR:
        // Honor RXFRQ: flush receive data, clear RXNE
        if (v & RQR_RXFRQ) {
            s.isr &= ~ISR_RXNE;
        }
        usart1_maybe_raise_irq();
        break;

    case USART_ICR:
        s.icr = v;
        // Honor TC clear if requested
        if (v & ICR_TCCF) {
            s.isr &= ~ISR_TC;
        }
        break;

    case USART_TDR: {
        // Writing TDR transmits one byte if UE & TE set
        s.tdr = (uint8_t)v;
        if ((s.cr1 & CR1_UE) && (s.cr1 & CR1_TE)) {
            // TXE goes low briefly while we "load" the shift register
            s.isr &= ~ISR_TXE;
            api_pty_write_req(s.pty_fd, (uint8_t)v);

            // For simplicity, complete immediately: set TXE and TC
            s.isr |= ISR_TXE;
            s.isr |= ISR_TC;

            dev_debug("USART1: TX byte written to PTY");
            usart1_maybe_raise_irq();
        }
        break;
    }

    default:
        dev_debug("USART1: Unhandled WRITE");
        break;
    }
}

/* ====== Initialization ====== */
void usart1_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&s, 0, sizeof(s));

    // Start in idle-like state so firmware will see TXE/TC and IDLE high
    usart1_set_idle_defaults();

    // Create PTY and start periodic RX polling
    s.pty_fd = api_pty_fd_gen();
    if (s.pty_fd < 0) {
        dev_debug("USART1: Failed to create PTY (/tmp/usart1_pty)");
        return;
    }
    dev_debug("USART1: PTY at /tmp/usart1_pty");

    s.pty_poll_timer = qemu_plugin_timer_new_period_ns(usart1_pty_poll_cb, NULL, PTY_POLL_PERIOD_NS);
    // After init, if firmware enables TE/RE/UE and sets TXEIE/RXNEIE, our ISR path
    // will mirror the hardware traces: ISR reads ~0x6000D0 and IRQ 37 firing.
}
