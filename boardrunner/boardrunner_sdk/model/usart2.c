#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define USART2_BASE         0x40004400ULL

#define USART_SR_OFF        0x00
#define USART_DR_OFF        0x04
#define USART_BRR_OFF       0x08
#define USART_CR1_OFF       0x0C
#define USART_CR2_OFF       0x10
#define USART_CR3_OFF       0x14

#define USART_SR_RXNE       (1U << 5)
#define USART_SR_TC         (1U << 6)
#define USART_SR_TXE        (1U << 7)

#define USART_CR1_RE        (1U << 2)
#define USART_CR1_TE        (1U << 3)
#define USART_CR1_UE        (1U << 13)

#define USART2_RXQ_SIZE     256
#define USART2_POLL_NS      1000000ULL

typedef struct {
    uint32_t sr;
    uint32_t dr;
    uint32_t brr;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3;
    int pty_fd;
    uint64_t poll_timer;
    uint8_t rxq[USART2_RXQ_SIZE];
    unsigned rxq_head;
    unsigned rxq_tail;
    unsigned rxq_count;
} USART2State;

static USART2State g_usart2;

static void usart2_ensure_pty(USART2State *s) {
    if (s->pty_fd < 0) {
        s->pty_fd = api_pty_fd_gen("usart2");
    }
}

static bool usart2_rxq_push(USART2State *s, uint8_t v) {
    if (s->rxq_count >= USART2_RXQ_SIZE) {
        return false;
    }
    s->rxq[s->rxq_head] = v;
    s->rxq_head = (s->rxq_head + 1U) % USART2_RXQ_SIZE;
    s->rxq_count++;
    return true;
}

static bool usart2_rxq_pop(USART2State *s, uint8_t *out) {
    if (s->rxq_count == 0U) {
        return false;
    }
    *out = s->rxq[s->rxq_tail];
    s->rxq_tail = (s->rxq_tail + 1U) % USART2_RXQ_SIZE;
    s->rxq_count--;
    return true;
}

static void usart2_host_poll(USART2State *s) {
    uint8_t ch;
    int rc;

    usart2_ensure_pty(s);
    if (s->pty_fd < 0) {
        return;
    }

    while (1) {
        rc = api_pty_read_nonblock(s->pty_fd, &ch);
        if (rc <= 0) {
            break;
        }
        usart2_rxq_push(s, ch);
    }
}

static void usart2_drain_rx(USART2State *s) {
    uint8_t ch;

    if ((s->cr1 & USART_CR1_UE) == 0U || (s->cr1 & USART_CR1_RE) == 0U) {
        return;
    }

    if ((s->sr & USART_SR_RXNE) != 0U) {
        return;
    }

    if (usart2_rxq_pop(s, &ch)) {
        s->dr = ch;
        s->sr |= USART_SR_RXNE;
    }
}

static void usart2_transmit_byte(USART2State *s, uint8_t ch) {
    s->dr = ch;

    if ((s->cr1 & USART_CR1_UE) != 0U && (s->cr1 & USART_CR1_TE) != 0U) {
        if (s->pty_fd >= 0) {
            api_pty_write_req(s->pty_fd, ch);
        }
    }
    s->sr |= (USART_SR_TXE | USART_SR_TC);
}

static void usart2_poll_cb(void *opaque) {
    USART2State *s = (USART2State *)opaque;
    usart2_host_poll(s);
    usart2_drain_rx(s);
}

void* usart2_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_usart2, 0, sizeof(g_usart2));
    g_usart2.pty_fd = api_pty_fd_gen("usart2");
    g_usart2.poll_timer = qemu_plugin_timer_new_period_ns(usart2_poll_cb, &g_usart2, USART2_POLL_NS);
    dev_debug("usart2: initialized with stateful USART registers and PTY backend\n");
    return &g_usart2;
}

uint64_t usart2_read(void *opaque, uint64_t addr, unsigned size) {
    USART2State *s = (USART2State *)opaque;
    uint64_t offset = addr - USART2_BASE;
    uint64_t ret = 0;

    (void)size;

    usart2_host_poll(s);
    usart2_drain_rx(s);

    switch (offset) {
    case USART_SR_OFF:
        ret = s->sr;
        break;
    case USART_DR_OFF:
        ret = s->dr & 0xFFU;
        s->sr &= ~USART_SR_RXNE;
        s->dr = 0;
        usart2_drain_rx(s);
        break;
    case USART_BRR_OFF:
        ret = s->brr;
        break;
    case USART_CR1_OFF:
        ret = s->cr1;
        break;
    case USART_CR2_OFF:
        ret = s->cr2;
        break;
    case USART_CR3_OFF:
        ret = s->cr3;
        break;
    default:
        ret = 0;
        break;
    }

    return ret;
}

void usart2_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    USART2State *s = (USART2State *)opaque;
    uint64_t offset = addr - USART2_BASE;
    uint8_t ch;

    (void)size;

    switch (offset) {
    case USART_SR_OFF:
        s->sr = (uint32_t)value;
        break;
    case USART_DR_OFF:
        ch = (uint8_t)(value & 0xFFU);
        usart2_transmit_byte(s, ch);
        break;
    case USART_BRR_OFF:
        s->brr = (uint32_t)value;
        break;
    case USART_CR1_OFF:
        s->cr1 = (uint32_t)value;
        usart2_drain_rx(s);
        break;
    case USART_CR2_OFF:
        s->cr2 = (uint32_t)value;
        break;
    case USART_CR3_OFF:
        s->cr3 = (uint32_t)value;
        break;
    default:
        break;
    }
}
