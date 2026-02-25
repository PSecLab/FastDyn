// Device Model for I2C1 (MAX78000_FTHR)
// Fixes the observed mismatch: emulated firmware stuck polling INTFL0==0xA0.
// Root causes (from provided traces):
//  1) Hardware shows INTFL0 transitions 0xA0 -> 0xA1/0xB1; emulated model never asserts these bits.
//  2) Hardware ordering sometimes: MSTCTRL kick first, then FIFO address write. Model must handle this.
// This model:
//  - Implements FIFO-driven master with an async “kick” engine using qemu_plugin timers.
//  - Asserts INTFL0 bit0 (done/data-ready) for BOTH write and read transactions.
//  - Asserts INTFL0 bit4 when RX length > 1 (to match 0xB1 vs 0xA1).
//  - Synthesizes STATUS {0x92B busy, 0xD29 idle, 0xD2D RX available}.
//  - Uses api_i2c_* bus bridging; address detection prefers configured bus slave addresses.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <device.h>
#include <boardrunner/vio.h>

#ifndef I2C1_BASE
#define I2C1_BASE 0x4001E000ULL
#endif

// Register offsets
#define REG_CTRL     0x00
#define REG_STATUS   0x04
#define REG_INTFL0   0x08
#define REG_INTEN0   0x0C
#define REG_INTFL1   0x10
#define REG_INTEN1   0x14
#define REG_FIFOLEN  0x18
#define REG_RXCTRL0  0x1C
#define REG_RXCTRL1  0x20
#define REG_TXCTRL0  0x24
#define REG_TXCTRL1  0x28
#define REG_FIFO     0x2C
#define REG_MSTCTRL  0x30
#define REG_CLKLO    0x34
#define REG_CLKHI    0x38

// Observed constants/patterns
#define FIFOLEN_CONST 0x00000808u

// INTFL0 reads observed: 0xA0, 0xA1, 0xB1
#define INTFL0_BASE   0x000000A0u
#define INTFL0_BIT0   0x00000001u
#define INTFL0_BIT4   0x00000010u

// STATUS reads observed: 0x92B, 0xD29, 0xD2D
#define STATUS_BUSY    0x0000092Bu
#define STATUS_IDLE    0x00000D29u
#define STATUS_RXAVAIL 0x00000D2Du

// Async kick delay: must be >0 so a FIFO write can happen after MSTCTRL kick (as in hardware trace)
#define EXEC_DELAY_NS  2000ULL

#define TX_FIFO_CAP 256
#define RX_FIFO_CAP 256

typedef struct {
    // register storage (write values; some have special readback)
    uint32_t ctrl_w;
    uint32_t inten0;
    uint32_t inten1;

    uint32_t rxctrl0;   // stored with flush bit stripped
    uint32_t rxctrl1;   // low 8 bits used as expected RX length
    uint32_t txctrl0;   // stored with flush bit stripped
    uint32_t txctrl1;

    uint32_t clklo;
    uint32_t clkhi;

    // CTRL boot-bit behavior (readback includes 0x300 until master-mode takes over)
    bool ctrl_boot_bits_active;

    // MSTCTRL readback: 0 until first write, then 1 always
    bool mstctrl_ready;

    // master enabled + kick waiting
    bool mst_enabled;
    bool waiting_for_fifo;   // kick fired but FIFO had no address yet
    bool busy;               // drives STATUS_BUSY

    // Latched INTFL0 extra bits (beyond base 0xA0)
    bool intfl0_b0;
    bool intfl0_b4;

    // FIFOs
    uint8_t tx_fifo[TX_FIFO_CAP];
    uint32_t tx_r, tx_w, tx_cnt;

    uint8_t rx_fifo[RX_FIFO_CAP];
    uint32_t rx_r, rx_w, rx_cnt;

    // I2C bus
    I2CBus bus;
    bool bus_inited;

    // timer
    uint64_t kick_timer;
    bool kick_timer_inited;

} i2c1_state_t;

static i2c1_state_t g_i2c1;

static inline uint32_t norm_off(hwaddr addr) {
    uint64_t a = (uint64_t)addr;
    if (a >= I2C1_BASE && a < (I2C1_BASE + 0x1000ULL)) {
        return (uint32_t)(a - I2C1_BASE);
    }
    return (uint32_t)addr;
}

static inline uint32_t size_mask_u32(unsigned size) {
    if (size >= 4) return 0xFFFFFFFFu;
    return (uint32_t)((1ULL << (size * 8)) - 1ULL);
}

// FIFO helpers
static inline void tx_push(i2c1_state_t *s, uint8_t b) {
    if (s->tx_cnt >= TX_FIFO_CAP) return;
    s->tx_fifo[s->tx_w % TX_FIFO_CAP] = b;
    s->tx_w++;
    s->tx_cnt++;
}
static inline bool tx_pop(i2c1_state_t *s, uint8_t *out) {
    if (s->tx_cnt == 0) return false;
    *out = s->tx_fifo[s->tx_r % TX_FIFO_CAP];
    s->tx_r++;
    s->tx_cnt--;
    return true;
}
static inline bool tx_peek(i2c1_state_t *s, uint8_t *out) {
    if (s->tx_cnt == 0) return false;
    *out = s->tx_fifo[s->tx_r % TX_FIFO_CAP];
    return true;
}

static inline void rx_push(i2c1_state_t *s, uint8_t b) {
    if (s->rx_cnt >= RX_FIFO_CAP) return;
    s->rx_fifo[s->rx_w % RX_FIFO_CAP] = b;
    s->rx_w++;
    s->rx_cnt++;
}
static inline bool rx_pop(i2c1_state_t *s, uint8_t *out) {
    if (s->rx_cnt == 0) return false;
    *out = s->rx_fifo[s->rx_r % RX_FIFO_CAP];
    s->rx_r++;
    s->rx_cnt--;
    return true;
}

static bool bus_has_addr7(const i2c1_state_t *s, uint8_t addr7) {
    if (!s->bus_inited) return false;
    for (int i = 0; i < s->bus.Slaves.num_slaves; i++) {
        if ((uint8_t)(s->bus.Slaves.slave[i].address & 0x7F) == (addr7 & 0x7F)) {
            return true;
        }
    }
    return false;
}

// Based only on provided data: address bytes observed in FIFO writes include 0xD0/0xE0/0xEC/0xED/0xF7.
// Prefer bus slave list, fall back to these.
static bool looks_like_addr_byte(const i2c1_state_t *s, uint8_t b) {
    uint8_t a7 = (uint8_t)((b >> 1) & 0x7F);
    if (bus_has_addr7(s, a7)) return true;
    switch (b) {
        case 0xD0: case 0xD1:
        case 0xE0: case 0xE1:
        case 0xEC: case 0xED:
        case 0xF6: case 0xF7:
            return true;
        default:
            return false;
    }
}

static inline uint32_t compute_intfl0(const i2c1_state_t *s) {
    uint32_t v = INTFL0_BASE;
    if (s->intfl0_b0) v |= INTFL0_BIT0;
    if (s->intfl0_b4) v |= INTFL0_BIT4;
    return v;
}

static inline uint32_t compute_status(const i2c1_state_t *s) {
    // Match observed set: busy/idle/rxavail
    if (s->rx_cnt > 0) return STATUS_RXAVAIL;
    if (s->busy) return STATUS_BUSY;
    return STATUS_IDLE;
}

// CTRL readback heuristic inferred from init trace transitions
static inline uint32_t readback_ctrl(i2c1_state_t *s) {
    uint32_t v = s->ctrl_w;

    const bool master_mode = (v & 0x400u) != 0;

    if (master_mode) {
        v &= ~0x300u; // drop boot bits once master-mode is active
        if ((v & 0x080u) && ((v & 0x200u) == 0)) {
            v |= 0x200u; // 0x481 -> 0x681 behavior
        }
        if (v & 0x040u) {
            v |= 0x100u; // 0x6C1 -> 0x7C1 behavior
        }
    } else {
        if (s->ctrl_boot_bits_active) {
            v |= 0x300u; // early reads look like 0x300 baseline
        }
    }

    return v;
}

// Forward decl
static void i2c1_kick_cb(void *data);

static void schedule_kick(i2c1_state_t *s) {
    if (!s->kick_timer_inited) return;
    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
    qemu_plugin_timer_alarm(s->kick_timer, now + EXEC_DELAY_NS);
}

// One “engine step”: consume one address segment and produce INTFL0/STATUS/FIFO behavior
static void i2c1_engine_step(i2c1_state_t *s) {
    if (!s->mst_enabled) return;

    // Need an address byte to do anything meaningful
    uint8_t b0;
    if (!tx_peek(s, &b0) || !looks_like_addr_byte(s, b0)) {
        // Hardware shows MSTCTRL kick can precede FIFO write; wait and let FIFO write re-trigger kick.
        s->waiting_for_fifo = true;
        s->busy = true; // reflect hardware STATUS=0x92B while waiting/active
        return;
    }

    (void)tx_pop(s, &b0);

    uint8_t addr7 = (uint8_t)((b0 >> 1) & 0x7F);
    bool is_recv = (b0 & 0x1u) != 0;

    s->busy = true;

    if (s->bus_inited) {
        (void)api_i2c_start_transfer(&s->bus, addr7, is_recv);
    }

    if (is_recv) {
        uint32_t rx_len = (uint32_t)(s->rxctrl1 & 0xFFu);
        if (rx_len == 0) rx_len = 1;

        for (uint32_t i = 0; i < rx_len; i++) {
            uint8_t rb = 0;
            if (s->bus_inited) rb = api_i2c_recv(&s->bus);
            rx_push(s, rb);
        }

        // Match INTFL0 read values: 0xA1 for 1B, 0xB1 for >1B
        s->intfl0_b0 = true;
        s->intfl0_b4 = (rx_len > 1);
    } else {
        // Send payload bytes until FIFO empty or next address (repeat-start style)
        while (s->tx_cnt > 0) {
            uint8_t nb;
            if (!tx_peek(s, &nb)) break;
            if (looks_like_addr_byte(s, nb)) break;

            (void)tx_pop(s, &nb);
            if (s->bus_inited) (void)api_i2c_send(&s->bus, nb);
        }

        // IMPORTANT FIX: assert done bit even for write transfers, otherwise firmware can poll forever.
        s->intfl0_b0 = true;
        s->intfl0_b4 = false;
    }

    if (s->bus_inited) {
        api_i2c_end_transfer(&s->bus);
    }

    s->busy = false;
}

static void i2c1_kick_cb(void *data) {
    i2c1_state_t *s = (i2c1_state_t *)data;
    s->waiting_for_fifo = false;
    i2c1_engine_step(s);
}

uint64_t i2c1_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque;
    i2c1_state_t *s = &g_i2c1;

    uint32_t off = norm_off(addr);
    uint32_t v = 0;

    switch (off) {
        case REG_CTRL:    v = readback_ctrl(s); break;
        case REG_STATUS:  v = compute_status(s); break;
        case REG_INTFL0:  v = compute_intfl0(s); break;
        case REG_INTEN0:  v = s->inten0; break;
        case REG_INTFL1:  v = 0; break; // traces read 0
        case REG_INTEN1:  v = s->inten1; break;
        case REG_FIFOLEN: v = FIFOLEN_CONST; break;
        case REG_RXCTRL0: v = s->rxctrl0; break;
        case REG_RXCTRL1: v = s->rxctrl1; break;
        case REG_TXCTRL0: v = s->txctrl0; break;
        case REG_TXCTRL1: v = s->txctrl1; break;

        case REG_FIFO: {
            uint8_t b = 0;
            if (rx_pop(s, &b)) v = (uint32_t)b;
            else v = 0; // hardware shows frequent 0 reads
            break;
        }

        case REG_MSTCTRL:
            v = s->mstctrl_ready ? 0x1u : 0x0u;
            break;

        case REG_CLKLO: v = s->clklo; break;
        case REG_CLKHI: v = s->clkhi; break;

        default:
            v = 0;
            break;
    }

    v &= size_mask_u32(size);
    return (uint64_t)v;
}

void i2c1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque;
    i2c1_state_t *s = &g_i2c1;

    uint32_t off = norm_off(addr);
    uint32_t v = ((uint32_t)value) & size_mask_u32(size);

    switch (off) {
        case REG_CTRL:
            s->ctrl_w = v;
            if (v & 0x400u) s->ctrl_boot_bits_active = false;
            break;

        case REG_INTEN0: s->inten0 = v; break;
        case REG_INTEN1: s->inten1 = v; break;

        case REG_INTFL0:
            // W1C behavior for our latched bits only; base bits (0xA0) remain “always-on”.
            if (v == 0xFFFFFFFFu || v == 0x00FFFFFFu) {
                s->intfl0_b0 = false;
                s->intfl0_b4 = false;
            } else {
                if (v & 0x1u)  s->intfl0_b0 = false;
                if (v & 0x10u) s->intfl0_b4 = false;
            }
            break;

        case REG_INTFL1:
            // writes observed {0,7}; reads observed 0
            break;

        case REG_RXCTRL0: {
            bool flush = (v & 0x80u) != 0;
            s->rxctrl0 = (v & ~0x80u);
            if (flush) {
                s->rx_cnt = s->rx_r = s->rx_w = 0;
            }
            break;
        }

        case REG_RXCTRL1:
            s->rxctrl1 = v;
            break;

        case REG_TXCTRL0: {
            bool flush = (v & 0x80u) != 0;
            s->txctrl0 = (v & ~0x80u);
            if (flush) {
                s->tx_cnt = s->tx_r = s->tx_w = 0;
            }
            break;
        }

        case REG_TXCTRL1:
            s->txctrl1 = v;
            break;

        case REG_FIFO:
            tx_push(s, (uint8_t)(v & 0xFFu));

            // Key fix for the observed ordering: if we previously kicked and were waiting for FIFO,
            // a FIFO write should trigger the engine soon.
            if (s->waiting_for_fifo && s->mst_enabled) {
                schedule_kick(s);
            }
            break;

        case REG_MSTCTRL:
            // After first write, reads are 1 in trace.
            s->mstctrl_ready = true;

            // In trace, writes are 1 and 3. We interpret:
            //  - bit0: enable master
            //  - bit1: kick/execute (3 means enable+kICK)
            if (v & 0x1u) s->mst_enabled = true;

            if (v & 0x2u) {
                // Kick is async: allows FIFO write to occur after this write (as in hardware trace).
                s->busy = true;
                schedule_kick(s);
            }
            break;

        case REG_CLKLO:
            s->clklo = (v & 0xFFu);
            s->ctrl_boot_bits_active = false;
            break;

        case REG_CLKHI:
            s->clkhi = (v & 0xFFu);
            s->ctrl_boot_bits_active = false;
            break;

        default:
            break;
    }
}

void i2c1_init(ConfigSection* model_info) {
    memset(&g_i2c1, 0, sizeof(g_i2c1));

    // CTRL initial readback behaves like 0x300 baseline even if written 0.
    g_i2c1.ctrl_w = 0x0;
    g_i2c1.ctrl_boot_bits_active = true;

    // MSTCTRL reads 0 until first write, then 1 forever.
    g_i2c1.mstctrl_ready = false;

    // Init I2C bus from config
    g_i2c1.bus = api_i2c_init_bus(model_info);
    g_i2c1.bus_inited = true;

    // Init one-shot kick timer
    g_i2c1.kick_timer = qemu_plugin_timer_new_ns(i2c1_kick_cb, &g_i2c1);
    g_i2c1.kick_timer_inited = (g_i2c1.kick_timer != 0);

    dev_debug("[i2c1] init: FIFO+MSTCTRL async engine enabled\n");
}
