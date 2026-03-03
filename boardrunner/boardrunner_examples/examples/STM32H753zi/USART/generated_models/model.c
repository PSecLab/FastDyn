// Device Model for USART3 (STM32H753x)
// Goal: match trace-observed CR1/CR3/ISR dynamics + basic RX/TX + IRQ pulse behavior.
//
// Observations to emulate (from traces):
// - CR1 readbacks: 0xD, 0x8D, 0x4D, 0x2D (and corresponding writes occur)
// - CR3 readbacks: 0x0 and sometimes 0x1 (writes occur)
// - ISR readbacks: 0x6000D0 baseline; sometimes 0x6010D0 (bit12 set);
//                  sometimes 0x6000F0 / 0x6010F0 (adds bit5-like RX-ready).
// - An interrupt vector "39 (0x27)" drives an ISR that accesses ONLY USART3;
//   we therefore pulse irq vector 39 when TXEIE/RXNEIE/TCIE conditions occur.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <device.h>
#include <boardrunner/vio.h>

#define USART3_BASE 0x40004800ULL

// Register offsets (USART v2 on STM32H7)
#define OFF_CR1     0x00
#define OFF_CR2     0x04
#define OFF_CR3     0x08
#define OFF_ISR     0x1C
#define OFF_RDR     0x24
#define OFF_TDR     0x28
#define OFF_PRESC   0x2C

// CR1 bit meanings used only to drive plausible interrupt behavior (not to overfit).
#define CR1_UE      (1u << 0)
#define CR1_RE      (1u << 2)
#define CR1_TE      (1u << 3)
#define CR1_RXNEIE  (1u << 5)   // matches 0x2D pattern (adds 0x20)
#define CR1_TCIE    (1u << 6)   // matches 0x4D pattern (adds 0x40)
#define CR1_TXEIE   (1u << 7)   // matches 0x8D pattern (adds 0x80)

// ISR bits (subset)
#define ISR_RXNE    (1u << 5)
#define ISR_TC      (1u << 6)
#define ISR_TXE     (1u << 7)
#define ISR_IDLE    (1u << 4)
#define ISR_BIT12   (1u << 12)  // trace delta: 0x6000D0 -> 0x6010D0
#define ISR_TEACK   (1u << 21)
#define ISR_REACK   (1u << 22)

// Trace-driven baseline
#define ISR_BASE_LOW  (ISR_IDLE | ISR_TC | ISR_TXE)   // 0xD0
#define IRQ_VECTOR_39 39                              // from trace (labeled UART4, but services USART3)
#define IRQ_LINE(irqn) ((irqn) + 16)                  // required by your API

typedef struct {
    // Stateful registers (RMW safe)
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3;
    uint32_t presc;

    // RX/TX state
    int      pty_fd;
    bool     rx_pending;
    uint8_t  rx_byte;

    bool     tx_busy;

    // Timers
    uint64_t tx_done_timer;
    uint64_t rx_poll_timer;

    // Edge-latching so we don't spam pulses
    bool txe_irq_armed;
    bool tc_irq_armed;
    bool rxne_irq_armed;
} usart3_state_t;

static usart3_state_t g_usart3;

static inline uint32_t off_norm(hwaddr addr)
{
    uint64_t a = (uint64_t)addr;
    if (a >= USART3_BASE && a < USART3_BASE + 0x400) {
        return (uint32_t)(a - USART3_BASE);
    }
    return (uint32_t)a;
}

static inline uint32_t compute_isr(usart3_state_t *s)
{
    // Build ISR from trace-consistent rules:
    // - Low bits default to 0xD0 (IDLE|TC|TXE) when not busy.
    // - Add RXNE (0x20) only if rx_pending.
    // - Add bit12 (0x1000) when CR3==1 (matches 0x6010D0 / 0x6010F0 cases).
    // - TEACK/REACK appear set in all trace ISR values -> tie them to UE+TE/RE.
    uint32_t isr = 0;

    // ack bits
    if (s->cr1 & CR1_UE) {
        if (s->cr1 & CR1_TE) isr |= ISR_TEACK;
        if (s->cr1 & CR1_RE) isr |= ISR_REACK;
    }

    // bit12 behavior linked to CR3 bit0 in traces (CR3 reads/writes 0/1)
    if (s->cr3 & 0x1u) {
        isr |= ISR_BIT12;
    }

    // TX status
    if (!s->tx_busy) {
        isr |= ISR_BASE_LOW;
    } else {
        // brief clearing while busy
        isr |= ISR_IDLE;
        // TC/TXE cleared while busy
    }

    // RX status
    if (s->rx_pending) {
        isr |= ISR_RXNE;
    }

    return isr;
}

static void pulse_irq_39(void)
{
    // API requires irq+16 and false (non-secure)
    qemu_plugin_raise_irq(IRQ_LINE(IRQ_VECTOR_39), false);
}

static void eval_irqs(usart3_state_t *s)
{
    uint32_t isr = compute_isr(s);

    bool txe_cond  = ((s->cr1 & CR1_TXEIE) != 0u) && ((isr & ISR_TXE) != 0u);
    bool tc_cond   = ((s->cr1 & CR1_TCIE)  != 0u) && ((isr & ISR_TC)  != 0u);
    bool rxne_cond = ((s->cr1 & CR1_RXNEIE)!= 0u) && ((isr & ISR_RXNE)!= 0u);

    // Edge-triggered pulse behavior
    if (txe_cond && !s->txe_irq_armed) { pulse_irq_39(); s->txe_irq_armed = true; }
    if (!txe_cond) s->txe_irq_armed = false;

    if (tc_cond && !s->tc_irq_armed) { pulse_irq_39(); s->tc_irq_armed = true; }
    if (!tc_cond) s->tc_irq_armed = false;

    if (rxne_cond && !s->rxne_irq_armed) { pulse_irq_39(); s->rxne_irq_armed = true; }
    if (!rxne_cond) s->rxne_irq_armed = false;
}

static void try_fill_rx(usart3_state_t *s)
{
    if (s->pty_fd < 0) return;
    if (s->rx_pending) return;

    uint8_t b = 0;
    int st = api_pty_read_nonblock(s->pty_fd, &b);
    if (st > 0) {
        s->rx_byte = b;
        s->rx_pending = true;
    }
}

static void rx_poll_cb(void *opaque)
{
    usart3_state_t *s = (usart3_state_t *)opaque;
    try_fill_rx(s);
    eval_irqs(s);
}

static void tx_done_cb(void *opaque)
{
    usart3_state_t *s = (usart3_state_t *)opaque;
    s->tx_busy = false;
    eval_irqs(s);
}

static void arm_oneshot(uint64_t timer, uint64_t delta_ns)
{
    int64_t now = qemu_plugin_get_virtual_timer();
    uint64_t fire_at = (uint64_t)now + delta_ns; // absolute
    qemu_plugin_timer_alarm(timer, fire_at);
}

// This function will emulate all device reads
uint64_t usart3_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    usart3_state_t *s = &g_usart3;
    uint32_t off = off_norm(addr);
    uint32_t val = 0;

    // keep RX state fresh even for polling firmware
    try_fill_rx(s);

    switch (off) {
    case OFF_CR1:   val = s->cr1; break;
    case OFF_CR2:   val = s->cr2; break;
    case OFF_CR3:   val = s->cr3; break;
    case OFF_PRESC: val = s->presc; break;

    case OFF_ISR:
        val = compute_isr(s);
        break;

    case OFF_RDR:
        // Reading RDR consumes RXNE-like state
        if (!s->rx_pending) {
            try_fill_rx(s);
        }
        if (s->rx_pending) {
            val = (uint32_t)s->rx_byte;
            s->rx_pending = false;
        } else {
            val = 0;
        }
        eval_irqs(s);
        break;

    default: {
        char buf[128];
        snprintf(buf, sizeof(buf), "USART3: READ unhandled off=0x%X size=%u\n", off, size);
        dev_debug(buf);
        val = 0;
        break;
    }
    }

    // Apply access size
    if (size == 1) return (uint8_t)(val & 0xFFu);
    if (size == 2) return (uint16_t)(val & 0xFFFFu);
    return (uint64_t)val;
}

// This function will emulate all device writes
void usart3_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    (void)opaque;
    usart3_state_t *s = &g_usart3;
    uint32_t off = off_norm(addr);

    uint32_t v32;
    if (size == 1) v32 = (uint32_t)(value & 0xFFu);
    else if (size == 2) v32 = (uint32_t)(value & 0xFFFFu);
    else v32 = (uint32_t)(value & 0xFFFFFFFFu);

    switch (off) {
    case OFF_CR1:
        // Fully stateful (RMW-safe). Firmware toggles among 0xD/0x8D/0x4D/0x2D.
        s->cr1 = v32;
        eval_irqs(s); // enabling IE bits can cause immediate IRQ if flags already set
        break;

    case OFF_CR2:
        s->cr2 = v32;
        break;

    case OFF_CR3:
        // Firmware toggles 0/1; ISR bit12 correlates with this.
        s->cr3 = v32;
        eval_irqs(s);
        break;

    case OFF_PRESC:
        s->presc = v32;
        break;

    case OFF_TDR: {
        // TX one byte to host PTY. In traces, writes occur inside the Vector39 handler.
        uint8_t b = (uint8_t)(v32 & 0xFFu);
        if (s->pty_fd >= 0) {
            api_pty_write_req(s->pty_fd, b);
        }

        // Briefly clear TXE/TC-like readiness, then restore quickly.
        s->tx_busy = true;
        s->txe_irq_armed = false; // writing TDR "services" TXE
        s->tc_irq_armed = false;  // and clears TC in real HW
        eval_irqs(s);

        // Re-complete quickly so polling reads still mostly see 0x...D0 like the traces.
        if (s->tx_done_timer) {
            arm_oneshot(s->tx_done_timer, 10000ULL); // 10 us
        }
        break;
    }

    default: {
        char buf[160];
        snprintf(buf, sizeof(buf), "USART3: WRITE unhandled off=0x%X size=%u val=0x%08X\n",
                 off, size, v32);
        dev_debug(buf);
        break;
    }
    }
}

void usart3_init(ConfigSection* model_info)
{
    (void)model_info;
    memset(&g_usart3, 0, sizeof(g_usart3));

    // Init trace shows CR1/CR2/CR3 are written as 0 at bring-up; we keep reset state = 0.
    g_usart3.cr1 = 0;
    g_usart3.cr2 = 0;
    g_usart3.cr3 = 0;
    g_usart3.presc = 0;

    // Host PTY endpoint (API states fixed path /tmp/usart1_pty)
    g_usart3.pty_fd = api_pty_fd_gen();
    if (g_usart3.pty_fd < 0) {
        dev_debug("USART3: api_pty_fd_gen failed; no host serial endpoint\n");
    } else {
        dev_debug("USART3: PTY ready at /tmp/usart1_pty\n");
    }

    // One-shot TX completion timer
    g_usart3.tx_done_timer = qemu_plugin_timer_new_ns(tx_done_cb, &g_usart3);

    // Periodic RX polling timer (keeps RXNE/IRQ behavior alive even if firmware relies on interrupts)
    // Choose 1ms polling to stay in the same ballpark as the trace’s ms-scale interrupt spacing.
    g_usart3.rx_poll_timer = qemu_plugin_timer_new_period_ns(rx_poll_cb, &g_usart3, 1000000ULL);

    dev_debug("USART3: init complete\n");
}