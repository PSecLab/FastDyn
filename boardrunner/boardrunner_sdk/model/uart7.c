#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define UART7_BASE          0x40007800ULL

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

#define UART7_RXQ_SIZE      256
#define UART7_POLL_NS       1000000ULL

typedef struct {
    uint32_t sr;
    uint32_t dr;
    uint32_t brr;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3;
    int pty_fd;
    uint64_t poll_timer;
    uint8_t rxq[UART7_RXQ_SIZE];
    unsigned rxq_head;
    unsigned rxq_tail;
    unsigned rxq_count;
} UART7State;

static UART7State g_uart7;

static void uart7_ensure_pty(UART7State *s) {
    if (s->pty_fd < 0) {
        s->pty_fd = api_pty_fd_gen("uart7");
    }
}

static bool uart7_rxq_push(UART7State *s, uint8_t v) {
    if (s->rxq_count >= UART7_RXQ_SIZE) {
        return false;
    }
    s->rxq[s->rxq_head] = v;
    s->rxq_head = (s->rxq_head + 1U) % UART7_RXQ_SIZE;
    s->rxq_count++;
    return true;
}

static bool uart7_rxq_pop(UART7State *s, uint8_t *out) {
    if (s->rxq_count == 0U) {
        return false;
    }
    *out = s->rxq[s->rxq_tail];
    s->rxq_tail = (s->rxq_tail + 1U) % UART7_RXQ_SIZE;
    s->rxq_count--;
    return true;
}

static void uart7_host_poll(UART7State *s) {
    uint8_t ch;
    int rc;

    uart7_ensure_pty(s);
    if (s->pty_fd < 0) {
        return;
    }

    while (1) {
        rc = api_pty_read_nonblock(s->pty_fd, &ch);
        if (rc <= 0) {
            break;
        }
        uart7_rxq_push(s, ch);
    }
}

static void uart7_drain_rx(UART7State *s) {
    uint8_t ch;

    if ((s->cr1 & USART_CR1_UE) == 0U || (s->cr1 & USART_CR1_RE) == 0U) {
        return;
    }

    if ((s->sr & USART_SR_RXNE) != 0U) {
        return;
    }

    if (uart7_rxq_pop(s, &ch)) {
        s->dr = ch;
        s->sr |= USART_SR_RXNE;
    }
}

static void uart7_transmit_byte(UART7State *s, uint8_t ch) {
    s->dr = ch;

    if ((s->cr1 & USART_CR1_UE) != 0U && (s->cr1 & USART_CR1_TE) != 0U) {
        uart7_ensure_pty(s);
        if (s->pty_fd >= 0) {
            api_pty_write_req(s->pty_fd, ch);
        }
    }

    s->sr |= (USART_SR_TXE | USART_SR_TC);
}

static void uart7_poll_cb(void *opaque) {
    UART7State *s = (UART7State *)opaque;
    uart7_host_poll(s);
    uart7_drain_rx(s);
}

void* uart7_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_uart7, 0, sizeof(g_uart7));
    g_uart7.pty_fd = api_pty_fd_gen("uart7");
    g_uart7.poll_timer = qemu_plugin_timer_new_period_ns(uart7_poll_cb, &g_uart7, UART7_POLL_NS);
    dev_debug("uart7: initialized with stateful USART registers and PTY backend\n");
    return &g_uart7;
}

uint64_t uart7_read(void *opaque, uint64_t addr, unsigned size) {
    UART7State *s = (UART7State *)opaque;
    uint64_t offset = addr - UART7_BASE;
    uint64_t ret = 0;

    (void)size;

    uart7_host_poll(s);
    uart7_drain_rx(s);

    switch (offset) {
    case USART_SR_OFF:
        ret = s->sr;
        break;
    case USART_DR_OFF:
        ret = s->dr & 0xFFU;
        s->sr &= ~USART_SR_RXNE;
        s->dr = 0;
        uart7_drain_rx(s);
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

void uart7_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    UART7State *s = (UART7State *)opaque;
    uint64_t offset = addr - UART7_BASE;
    uint8_t ch;

    (void)size;

    switch (offset) {
    case USART_SR_OFF:
        s->sr = (uint32_t)value;
        break;
    case USART_DR_OFF:
        ch = (uint8_t)(value & 0xFFU);
        uart7_transmit_byte(s, ch);
        break;
    case USART_BRR_OFF:
        s->brr = (uint32_t)value;
        break;
    case USART_CR1_OFF:
        s->cr1 = (uint32_t)value;
        uart7_drain_rx(s);
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
