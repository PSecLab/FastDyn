// Device Model for USART2

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Inferred Register Functions:
// SR   (0x00): status register, dynamic flags TXE/TC/RXNE/IDLE/ORE
// DR   (0x04): transmit/receive data register
// BRR  (0x08): baud-rate register
// CR1  (0x0C): main control register (UE/RE/TE/RXNEIE observed)
// CR2  (0x10): secondary control register
// CR3  (0x14): tertiary control register

#define USART2_BASE_ADDR 0x40004400ULL
#define USART2_IRQ_NUM   54   /* NVIC IRQn 38 => exception 54 */

/* Register offsets */
#define USART_SR_OFF     0x00
#define USART_DR_OFF     0x04
#define USART_BRR_OFF    0x08
#define USART_CR1_OFF    0x0C
#define USART_CR2_OFF    0x10
#define USART_CR3_OFF    0x14
#define USART_GTPR_OFF   0x18

/* SR bits (STM32F1 USART) */
#define USART_SR_PE      (1u << 0)
#define USART_SR_FE      (1u << 1)
#define USART_SR_NE      (1u << 2)
#define USART_SR_ORE     (1u << 3)
#define USART_SR_IDLE    (1u << 4)
#define USART_SR_RXNE    (1u << 5)
#define USART_SR_TC      (1u << 6)
#define USART_SR_TXE     (1u << 7)
#define USART_SR_LBD     (1u << 8)
#define USART_SR_CTS     (1u << 9)

/* CR1 bits used by firmware */
#define USART_CR1_RE     (1u << 2)
#define USART_CR1_TE     (1u << 3)
#define USART_CR1_RXNEIE (1u << 5)
#define USART_CR1_TCIE   (1u << 6)
#define USART_CR1_TXEIE  (1u << 7)
#define USART_CR1_UE     (1u << 13)

typedef struct {
    uint32_t brr;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3;

    /* Latched non-TX-completion status bits */
    uint32_t latched_sr;

    /* Receive data holding register */
    uint8_t  rx_byte;

    /* STM32 clears some flags by SR read followed by DR read */
    bool     sr_read_for_clear;

    /* Host serial endpoint */
    int      pty_fd;

    /* Background polling timer */
    uint64_t poll_timer;

    /* Hardwired from trace */
    int      irq_num;

    /* Transmit status timing: TXE rises before TC after a DR write */
    int64_t  txe_ready_deadline_ns;
    int64_t  tx_complete_deadline_ns;
} USART2State;

static USART2State g_usart2;

static uint64_t usart2_mask_by_size(uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        return value & 0xFFu;
    case 2:
        return value & 0xFFFFu;
    default:
        return value & 0xFFFFFFFFu;
    }
}

static bool usart2_enabled(USART2State *s)
{
    return (s->cr1 & USART_CR1_UE) != 0;
}

static bool usart2_rx_enabled(USART2State *s)
{
    return usart2_enabled(s) && ((s->cr1 & USART_CR1_RE) != 0);
}

static bool usart2_tx_enabled(USART2State *s)
{
    return usart2_enabled(s) && ((s->cr1 & USART_CR1_TE) != 0);
}

/*
 * Approximate byte time from BRR.
 * We only need a coarse delay so TC can transiently drop after DR writes.
 * Assume a 16 MHz peripheral clock, which matches the traced BRR=0x682B
 * reasonably well for a low baud rate setup.
 */
static uint64_t usart2_frame_time_ns(USART2State *s)
{
    uint32_t div16;
    uint64_t baud;

    if (s->brr == 0) {
        return 1000000ULL; /* safe default: 1 ms */
    }

    div16 = s->brr & 0xFFFFu;
    if (div16 == 0) {
        return 1000000ULL;
    }

    baud = (16000000ULL * 16ULL) / div16;
    if (baud == 0) {
        return 1000000ULL;
    }

    /* ~10 bits per frame (start + 8 data + stop) */
    return (10000000000ULL / baud);
}

static void usart2_update_tx_flags(USART2State *s)
{
    int64_t now;

    if (!usart2_tx_enabled(s)) {
        s->latched_sr &= ~(USART_SR_TXE | USART_SR_TC);
        s->txe_ready_deadline_ns = 0;
        s->tx_complete_deadline_ns = 0;
        return;
    }

    now = qemu_plugin_get_virtual_timer();

    /*
     * On STM32 USART, a DR write clears both TXE and TC.
     * TXE rises first when the data register becomes empty again,
     * and TC rises later when the frame has fully completed.
     */
    if (s->txe_ready_deadline_ns <= 0 || now >= s->txe_ready_deadline_ns) {
        s->latched_sr |= USART_SR_TXE;
        s->txe_ready_deadline_ns = 0;
    } else {
        s->latched_sr &= ~USART_SR_TXE;
    }

    if (s->tx_complete_deadline_ns <= 0 || now >= s->tx_complete_deadline_ns) {
        s->latched_sr |= USART_SR_TC;
        s->tx_complete_deadline_ns = 0;
    } else {
        s->latched_sr &= ~USART_SR_TC;
    }
}

static void usart2_raise_irq_if_needed(USART2State *s)
{
    if (!usart2_enabled(s)) {
        return;
    }

    /*
     * RX side: RXNEIE covers RXNE/ORE.
     * TX side: TXEIE covers TXE; TCIE covers TC.
     * port driver, which keeps TXEIE armed during response transmission,
     * actually receives an interrupt for each successive byte.
     */
    if ((s->cr1 & USART_CR1_RXNEIE) &&
        (s->latched_sr & (USART_SR_RXNE | USART_SR_ORE))) {
        qemu_plugin_raise_irq(s->irq_num, false);
        return;
    }

    if ((s->cr1 & USART_CR1_TXEIE) &&
        (s->latched_sr & USART_SR_TXE)) {
        qemu_plugin_raise_irq(s->irq_num, false);
        return;
    }

    if ((s->cr1 & USART_CR1_TCIE) &&
        (s->latched_sr & USART_SR_TC)) {
        qemu_plugin_raise_irq(s->irq_num, false);
    }
}

static void usart2_accept_rx_byte(USART2State *s, uint8_t value)
{
    if (!usart2_rx_enabled(s)) {
        return;
    }

    if (s->latched_sr & USART_SR_RXNE) {
        /* Overrun: keep existing unread byte, drop the new one */
        s->latched_sr |= USART_SR_ORE;
    } else {
        s->rx_byte = value;
        s->latched_sr |= USART_SR_RXNE;
    }

    /*
     * The trace shows first receive-side ISR often seeing IDLE together with RXNE.
     * Latch IDLE alongside received traffic; clear it on SR->DR sequence.
     */
    s->latched_sr |= USART_SR_IDLE;
    s->sr_read_for_clear = false;

    usart2_raise_irq_if_needed(s);
}

static void usart2_poll_host_rx(USART2State *s)
{
    uint8_t ch;
    int ret;

    if (s->pty_fd < 0 || !usart2_rx_enabled(s)) {
        return;
    }

    /*
     * Model the single-byte receive holding behavior conservatively:
     * do not pull more host data while RXNE is already set, otherwise
     * tight MMIO polling can fabricate immediate ORE conditions that
     * are not present in the hardware trace.
     */
    if (s->latched_sr & USART_SR_RXNE) {
        return;
    }

    ret = api_pty_read_nonblock(s->pty_fd, &ch);
    if (ret > 0) {
        usart2_accept_rx_byte(s, ch);
    }
}

static uint32_t usart2_get_sr(USART2State *s)
{
    usart2_update_tx_flags(s);

    /*
     * Treat RX interrupt sources as level-like while the status remains pending.
     * A single pulse at byte-arrival time can be lost if the core/NVIC is not
     * yet ready; re-assert on later observable activity so the pending RXNE/ORE
     * condition can still vector the guest into the USART ISR.
     */
    usart2_raise_irq_if_needed(s);

    if (!usart2_enabled(s)) {
        return 0;
    }

    return s->latched_sr;
}

static void usart2_poll_timer_cb(void *opaque)
{
    USART2State *s = (USART2State *)opaque;

    usart2_poll_host_rx(s);
    usart2_update_tx_flags(s);
    usart2_raise_irq_if_needed(s);
}

static void usart2_write_dr(USART2State *s, uint64_t value)
{
    uint8_t ch = (uint8_t)(value & 0xFFu);
    uint64_t frame_ns;
    uint64_t txe_delay_ns;
    int64_t now;

    if (!usart2_tx_enabled(s)) {
        return;
    }

    if (s->pty_fd >= 0) {
        api_pty_write_req(s->pty_fd, ch);
    }

    /*
     * Hardware clears both TXE and TC on DR write.
     * TXE rises first (data register empty again), then TC rises when
     * the whole frame has left the line.
     */
    frame_ns = usart2_frame_time_ns(s);
    txe_delay_ns = frame_ns / 10ULL;
    if (txe_delay_ns == 0) {
        txe_delay_ns = 1;
    }

    now = qemu_plugin_get_virtual_timer();

    s->latched_sr &= ~(USART_SR_TXE | USART_SR_TC);
    s->txe_ready_deadline_ns = now + (int64_t)txe_delay_ns;
    s->tx_complete_deadline_ns = now + (int64_t)frame_ns;
}

static void usart2_apply_cr1(USART2State *s, uint32_t value)
{
    s->cr1 = value & 0x3FFFu;

    if (!usart2_rx_enabled(s)) {
        s->latched_sr &= ~(USART_SR_RXNE | USART_SR_IDLE | USART_SR_ORE);
        s->sr_read_for_clear = false;
    }

    if (!usart2_tx_enabled(s)) {
        s->latched_sr &= ~(USART_SR_TXE | USART_SR_TC);
        s->txe_ready_deadline_ns = 0;
        s->tx_complete_deadline_ns = 0;
    } else {
        /*
         * When transmitter is enabled and no transmit is in flight,
         * the idle state reads as TXE=1 and TC=1.
         */
        if (s->txe_ready_deadline_ns == 0 && s->tx_complete_deadline_ns == 0) {
            s->latched_sr |= (USART_SR_TXE | USART_SR_TC);
        }
    }

    /*
     * If RX was enabled or RXNEIE was just turned on, sample one waiting host byte
     * and raise IRQ immediately if receive status is now pending.
     */
    usart2_poll_host_rx(s);
    usart2_raise_irq_if_needed(s);
}

static void usart2_apply_cr2(USART2State *s, uint32_t value)
{
    s->cr2 = value & 0xFFFFu;
}

static void usart2_apply_cr3(USART2State *s, uint32_t value)
{
    s->cr3 = value & 0xFFFFu;
}

// This function will emulate all device reads
uint64_t usart2_read(void *opaque, hwaddr addr, unsigned size)
{
    USART2State *s = (USART2State *)opaque;
    hwaddr offset = addr - USART2_BASE_ADDR;
    uint32_t value = 0;

    /* Absolute addressing: convert to peripheral-relative offset first */
    if (addr < USART2_BASE_ADDR || addr >= (USART2_BASE_ADDR + 0x400)) {
        return 0;
    }

    /*
     * Poll host input synchronously on MMIO access as timer callbacks may lag
     * during tight guest loops.
     */
    usart2_poll_host_rx(s);

    switch (offset) {
    case USART_SR_OFF:
        value = usart2_get_sr(s);
        if (value & (USART_SR_RXNE | USART_SR_IDLE | USART_SR_ORE |
                     USART_SR_PE   | USART_SR_FE   | USART_SR_NE)) {
            s->sr_read_for_clear = true;
        }
        return usart2_mask_by_size(value, size);

    case USART_DR_OFF:
        value = (s->latched_sr & USART_SR_RXNE) ? s->rx_byte : 0;

        if (s->latched_sr & USART_SR_RXNE) {
            s->latched_sr &= ~USART_SR_RXNE;
        }

        if (s->sr_read_for_clear) {
            s->latched_sr &= ~(USART_SR_IDLE | USART_SR_ORE |
                               USART_SR_PE   | USART_SR_FE  |
                               USART_SR_NE);
        }
        s->sr_read_for_clear = false;

        /*
         * Hardware can present the next queued receive byte very quickly after
         * DR is drained. Re-sample host input immediately so back-to-back RX
         * traffic and interrupt retriggering behave more like the real USART.
         */
        usart2_poll_host_rx(s);
        usart2_raise_irq_if_needed(s);

        return usart2_mask_by_size(value, size);

    case USART_BRR_OFF:
        return usart2_mask_by_size(s->brr, size);

    case USART_CR1_OFF:
        return usart2_mask_by_size(s->cr1, size);

    case USART_CR2_OFF:
        return usart2_mask_by_size(s->cr2, size);

    case USART_CR3_OFF:
        return usart2_mask_by_size(s->cr3, size);

    case USART_GTPR_OFF:
        return 0;

    default:
        return 0;
    }
}

// This function will emulate all device writes
void usart2_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    USART2State *s = (USART2State *)opaque;
    hwaddr offset = addr - USART2_BASE_ADDR;
    (void)size;

    /* Absolute addressing: convert to peripheral-relative offset first */
    if (addr < USART2_BASE_ADDR || addr >= (USART2_BASE_ADDR + 0x400)) {
        return;
    }

    switch (offset) {
    case USART_SR_OFF:
        /*
         * Not observed in trace. Support minimal clear-on-write for TC/LBD/CTS
         * by clearing bits written as 0.
         */
        if ((value & USART_SR_TC) == 0) {
            s->latched_sr &= ~USART_SR_TC;
        }
        if ((value & USART_SR_LBD) == 0) {
            s->latched_sr &= ~USART_SR_LBD;
        }
        if ((value & USART_SR_CTS) == 0) {
            s->latched_sr &= ~USART_SR_CTS;
        }
        break;

    case USART_DR_OFF:
        usart2_write_dr(s, value);
        break;

    case USART_BRR_OFF:
        s->brr = (uint32_t)(value & 0xFFFFu);
        break;

    case USART_CR1_OFF:
        usart2_apply_cr1(s, (uint32_t)value);
        break;

    case USART_CR2_OFF:
        usart2_apply_cr2(s, (uint32_t)value);
        break;

    case USART_CR3_OFF:
        usart2_apply_cr3(s, (uint32_t)value);
        break;

    case USART_GTPR_OFF:
        /* Not used by traced firmware */
        break;

    default:
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* usart2_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_usart2, 0, sizeof(g_usart2));

    g_usart2.pty_fd = api_pty_fd_gen();
    g_usart2.irq_num = USART2_IRQ_NUM;
    g_usart2.poll_timer = qemu_plugin_timer_new_period_ns(usart2_poll_timer_cb,
                                                          &g_usart2,
                                                          1000000ULL); /* 1 ms */
    g_usart2.tx_complete_deadline_ns = 0;

    return &g_usart2;
}