#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/dma.h>
#include <boardrunner/vio.h>

#define USART6_BASE         0x40011400ULL

#define USART_SR_OFF        0x00
#define USART_DR_OFF        0x04
#define USART_BRR_OFF       0x08
#define USART_CR1_OFF       0x0C
#define USART_CR2_OFF       0x10
#define USART_CR3_OFF       0x14

#define USART_CR3_DMAR      (1U << 6)

#define USART_SR_RXNE       (1U << 5)
#define USART_SR_TC         (1U << 6)
#define USART_SR_TXE        (1U << 7)

#define USART_CR1_RE        (1U << 2)
#define USART_CR1_TE        (1U << 3)
#define USART_CR1_UE        (1U << 13)

#define USART6_RXQ_SIZE     2048
#define USART6_TXQ_SIZE     8192
#define USART6_POLL_NS      1000000ULL

typedef struct {
    uint32_t sr;
    uint32_t dr;
    uint32_t brr;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3;
    int pty_fd;
    uint64_t poll_timer;
    uint8_t rxq[USART6_RXQ_SIZE];
    unsigned rxq_head;
    unsigned rxq_tail;
    unsigned rxq_count;
    uint8_t txq[USART6_TXQ_SIZE];
    unsigned txq_head;
    unsigned txq_tail;
    unsigned txq_count;
} USART6State;

static USART6State g_usart6;

static void usart6_ensure_pty(USART6State *s) {
    if (s->pty_fd < 0) {
        s->pty_fd = api_pty_fd_gen("usart6");
    }
}

static bool usart6_rxq_push(USART6State *s, uint8_t v) {
    if (s->rxq_count >= USART6_RXQ_SIZE) {
        return false;
    }
    s->rxq[s->rxq_head] = v;
    s->rxq_head = (s->rxq_head + 1U) % USART6_RXQ_SIZE;
    s->rxq_count++;
    return true;
}

static bool usart6_rxq_pop(USART6State *s, uint8_t *out) {
    if (s->rxq_count == 0U) {
        return false;
    }
    *out = s->rxq[s->rxq_tail];
    s->rxq_tail = (s->rxq_tail + 1U) % USART6_RXQ_SIZE;
    s->rxq_count--;
    return true;
}

static bool usart6_txq_push(USART6State *s, uint8_t v) {
    if (s->txq_count >= USART6_TXQ_SIZE) {
        return false;
    }
    s->txq[s->txq_head] = v;
    s->txq_head = (s->txq_head + 1U) % USART6_TXQ_SIZE;
    s->txq_count++;
    return true;
}

static bool usart6_txq_pop(USART6State *s, uint8_t *out) {
    if (s->txq_count == 0U) {
        return false;
    }
    *out = s->txq[s->txq_tail];
    s->txq_tail = (s->txq_tail + 1U) % USART6_TXQ_SIZE;
    s->txq_count--;
    return true;
}

static void usart6_drain_tx(USART6State *s) {
    uint8_t ch;
    int rc;

    if ((s->cr1 & USART_CR1_UE) == 0U || (s->cr1 & USART_CR1_TE) == 0U) {
        return;
    }

    usart6_ensure_pty(s);
    if (s->pty_fd < 0) {
        return;
    }

    while (s->txq_count > 0U) {
        ch = s->txq[s->txq_tail];
        rc = api_pty_write_req(s->pty_fd, ch);
        if (rc <= 0) {
            break;
        }
        (void)usart6_txq_pop(s, &ch);
    }
}

static void usart6_host_poll(USART6State *s) {
    uint8_t ch;
    int rc;

    usart6_ensure_pty(s);
    if (s->pty_fd < 0) {
        return;
    }

    while (1) {
        rc = api_pty_read_nonblock(s->pty_fd, &ch);
        if (rc <= 0) {
            break;
        }
        usart6_rxq_push(s, ch);
    }
}

static void usart6_drain_rx(USART6State *s) {
    uint8_t ch;

    if ((s->cr1 & USART_CR1_UE) == 0U || (s->cr1 & USART_CR1_RE) == 0U) {
        return;
    }

    /* If DMA RX is enabled, bypass the RXNE flag and push all available bytes directly. */
    if ((s->cr3 & USART_CR3_DMAR) != 0U) {
        while (s->rxq_count > 0U) {
            ch = s->rxq[s->rxq_tail];
            if (api_dma_request_data(2, 1, USART6_BASE + USART_DR_OFF, &ch, 1) < 0) {
                break;
            }
            (void)usart6_rxq_pop(s, &ch);
            s->dr = ch;
            s->sr &= ~USART_SR_RXNE; /* Clear RXNE to prevent stalling */
        }
        return;
    }

    if ((s->sr & USART_SR_RXNE) != 0U) {
        return;
    }

    if (usart6_rxq_pop(s, &ch)) {
        s->dr = ch;
        s->sr |= USART_SR_RXNE;
    }
}

static void usart6_transmit_byte(USART6State *s, uint8_t ch) {
    s->dr = ch;

    if ((s->cr1 & USART_CR1_UE) != 0U && (s->cr1 & USART_CR1_TE) != 0U) {
        if (usart6_txq_push(s, ch)) {
            usart6_drain_tx(s);
        } else {
            dev_debug("usart6: TX queue full, dropping byte\n");
        }
    }

    s->sr |= (USART_SR_TXE | USART_SR_TC);
}

static void usart6_dma_tx_handler(void *opaque, const uint8_t *data, int len) {
    USART6State *s = (USART6State *)opaque;

    if (data == NULL || len <= 0) {
        return;
    }

    for (int i = 0; i < len; i++) {
        usart6_transmit_byte(s, data[i]);
    }
}

static void usart6_poll_cb(void *opaque) {
    USART6State *s = (USART6State *)opaque;
    usart6_drain_tx(s);
    usart6_host_poll(s);
    usart6_drain_rx(s);
}

void* usart6_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_usart6, 0, sizeof(g_usart6));
    g_usart6.pty_fd = api_pty_fd_gen("usart6");
    api_dma_register_stream_data(2, 7, usart6_dma_tx_handler, &g_usart6);
    g_usart6.poll_timer = qemu_plugin_timer_new_period_ns(usart6_poll_cb, &g_usart6, USART6_POLL_NS);
    dev_debug("usart6: initialized with PTY-backed peer\n");
    return &g_usart6;
}

uint64_t usart6_read(void *opaque, uint64_t addr, unsigned size) {
    USART6State *s = (USART6State *)opaque;
    uint64_t offset = addr - USART6_BASE;
    uint64_t ret = 0;

    (void)size;

    usart6_host_poll(s);
    usart6_drain_tx(s);
    usart6_drain_rx(s);

    switch (offset) {
    case USART_SR_OFF:
        ret = s->sr;
        break;
    case USART_DR_OFF:
        ret = s->dr & 0xFFU;
        s->sr &= ~USART_SR_RXNE;
        s->dr = 0;
        usart6_drain_rx(s);
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

void usart6_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    USART6State *s = (USART6State *)opaque;
    uint64_t offset = addr - USART6_BASE;
    uint8_t ch;

    (void)size;

    switch (offset) {
    case USART_SR_OFF:
        s->sr = (uint32_t)value;
        break;
    case USART_DR_OFF:
        ch = (uint8_t)(value & 0xFFU);
        usart6_transmit_byte(s, ch);
        break;
    case USART_BRR_OFF:
        s->brr = (uint32_t)value;
        break;
    case USART_CR1_OFF:
        s->cr1 = (uint32_t)value;
        usart6_drain_tx(s);
        usart6_drain_rx(s);
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
