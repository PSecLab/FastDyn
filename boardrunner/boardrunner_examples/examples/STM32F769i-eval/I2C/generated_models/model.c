// Device Model for I2C1

// Inferred Register Functions:
// - CR1.PE=0 resets peripheral state (TXE=1, BUSY=0, flags cleared).
// - CR2 holds SADD/RD_WRN/ADD10/NBYTES/AUTOEND/RELOAD; START/STOP are actions.
// - TX Preload: Firmware polls ISR for TXE=1 (0x1) and writes to TXDR *before* START.
// - START(TX): Model sends preloaded byte, sets BUSY=1. If NBYTES > 1, sets TXIS=1 (state 0x8002).
// - START(RX): Model sets BUSY=1, preloads RXDR, and sets ISR = BUSY|RXNE|TXE (0x8005).
// - TXDR Write (post-START): Sends byte, clears TXIS. If more bytes, sets TXIS=1 (state 0x8002).
// - RXDR Read: Returns byte, clears RXNE. If more bytes, preloads next byte and sets ISR back to 0x8005.

#include <device.h>
#include <devmodels_apis.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "utils.h"

// -------------------- Base & Offsets --------------------
#define I2C1_BASE         0x40005400u
#define I2C_CR1_OFFSET    0x00u
#define I2C_CR2_OFFSET    0x04u
#define I2C_OAR1_OFFSET   0x08u
#define I2C_OAR2_OFFSET   0x0Cu
#define I2C_TIMINGR_OFFSET 0x10u
#define I2C_ISR_OFFSET    0x18u
#define I2C_ICR_OFFSET    0x1Cu
#define I2C_RXDR_OFFSET   0x24u
#define I2C_TXDR_OFFSET   0x28u

// -------------------- Bitfields -------------------------
// CR1
#define I2C_CR1_PE        (1u << 0)

// CR2
#define I2C_CR2_SADD_Pos  0
#define I2C_CR2_SADD_Msk  (0x3FFu << I2C_CR2_SADD_Pos)
#define I2C_CR2_RD_WRN    (1u << 10)
#define I2C_CR2_ADD10     (1u << 11)
#define I2C_CR2_START     (1u << 13)
#define I2C_CR2_STOP      (1u << 14)
#define I2C_CR2_NBYTES_Pos 16
#define I2C_CR2_NBYTES_Msk (0xFFu << I2C_CR2_NBYTES_Pos)
#define I2C_CR2_RELOAD    (1u << 24)
#define I2C_CR2_AUTOEND   (1u << 25)

// ISR
#define I2C_ISR_TXE       (1u << 0)
#define I2C_ISR_TXIS      (1u << 1)
#define I2C_ISR_RXNE      (1u << 2)
#define I2C_ISR_ADDR      (1u << 3)
#define I2C_ISR_NACKF     (1u << 4)
#define I2C_ISR_STOPF     (1u << 5)
#define I2C_ISR_TC        (1u << 6)
#define I2C_ISR_TCR       (1u << 7)
#define I2C_ISR_BUSY      (1u << 15)

// ICR (W1C)
#define I2C_ICR_ADDRCF    (1u << 3)
#define I2C_ICR_NACKCF    (1u << 4)
#define I2C_ICR_STOPCF    (1u << 5)

// -------------------- Internal State --------------------
typedef enum {
    PHASE_IDLE = 0,
    PHASE_XFER_TX,
    PHASE_XFER_RX
} i2c_phase_t;

typedef struct {
    // guest-visible mirror
    uint32_t cr1, cr2, oar1, oar2, timingr, isr;
    uint8_t  rxdr;
    uint8_t  txdr; // Holds preloaded byte

    // protocol state
    i2c_phase_t phase;
    uint16_t sadd_latched;
    bool     is_10bit;
    bool     is_recv;
    uint8_t  nbytes;
    uint8_t  moved;

    // I2C bus handle
    I2CBus   bus;
} I2C1State;

static I2C1State s;

static inline void dd(const char *m) { dev_debug((char*)m); }

static inline void i2c_reset_state(void) {
    s.phase = PHASE_IDLE;
    s.sadd_latched = 0;
    s.is_10bit = false;
    s.is_recv = false;
    s.nbytes = 0;
    s.moved = 0;
    s.rxdr = 0;
    s.txdr = 0;
    // **FIX**: Set ISR to 0x1 (TXE=1), which matches HW Preload Loop 2
    s.isr = I2C_ISR_TXE;
}

static inline uint8_t extract_nbytes(uint32_t cr2) {
    uint8_t nb = (uint8_t)((cr2 & I2C_CR2_NBYTES_Msk) >> I2C_CR2_NBYTES_Pos);
    return nb;
}

static inline void latch_from_cr2(void) {
    s.is_10bit     = (s.cr2 & I2C_CR2_ADD10) != 0;
    s.is_recv      = (s.cr2 & I2C_CR2_RD_WRN) != 0;
    s.sadd_latched = (uint16_t)((s.cr2 & I2C_CR2_SADD_Msk) >> I2C_CR2_SADD_Pos);
    s.nbytes       = extract_nbytes(s.cr2);
    s.moved        = 0;
}

static inline void end_transfer_with_stop(void) {
    api_i2c_end_transfer(&s.bus);
    s.phase = PHASE_IDLE;
    s.isr &= ~(I2C_ISR_TC | I2C_ISR_TCR | I2C_ISR_RXNE | I2C_ISR_TXIS | I2C_ISR_BUSY);
    // Set STOPF and TXE (for next preload)
    s.isr |= (I2C_ISR_STOPF | I2C_ISR_TXE);
}

static inline void handle_tx_completion(void) {
    if (s.cr2 & I2C_CR2_RELOAD) {
        s.isr |= I2C_ISR_TCR;
        // **FIX**: Set TXIS, not TXE, for next byte
        s.isr |= I2C_ISR_TXIS;
    } else {
        s.isr |= I2C_ISR_TC;
        if (s.cr2 & I2C_CR2_AUTOEND) {
            end_transfer_with_stop();
        } else {
            // No AUTOEND, just sit with TC=1
            // **FIX**: Set TXIS, not TXE. Also set TXE as it's empty.
            s.isr |= (I2C_ISR_TXE | I2C_ISR_TXIS);
        }
    }
}

static inline void start_transaction(void) {
    s.isr &= ~(I2C_ISR_NACKF | I2C_ISR_STOPF | I2C_ISR_TC | I2C_ISR_TCR | I2C_ISR_ADDR);
    latch_from_cr2();

    int rc;
    if (s.is_10bit) {
        rc = api_i2c_start_transfer_10bit(&s.bus, s.sadd_latched, s.is_recv);
    } else {
        uint8_t addr7 = (uint8_t)((s.sadd_latched >> 1) & 0x7Fu);
        rc = api_i2c_start_transfer(&s.bus, addr7, s.is_recv);
    }

    if (rc != 0) {
        s.phase = PHASE_IDLE;
        s.isr |= I2C_ISR_NACKF;
        s.isr |= I2C_ISR_TXE; // Ready for next attempt
        return;
    }

    s.isr |= I2C_ISR_BUSY;

    if (s.is_recv) {
        // RX mode
        s.phase = PHASE_XFER_RX;
        if (s.nbytes > 0) {
             s.rxdr = api_i2c_recv(&s.bus);
             s.moved = 1;
             // **FIX**: Set state to 0x8005 (BUSY|RXNE|TXE) to match HW RX Loop
             s.isr |= (I2C_ISR_RXNE | I2C_ISR_TXE);
        } else {
             handle_tx_completion();
        }
    } else {
        // TX mode
        s.phase = PHASE_XFER_TX;
        // TXE is already 0 from the preload write
        if (s.nbytes > 0) {
            int tx_rc = api_i2c_send(&s.bus, s.txdr);
            s.moved = 1;
            if (tx_rc != 0) {
                s.isr |= I2C_ISR_NACKF;
            }

            if (s.moved < s.nbytes) {
                // **FIX**: Set TXIS (bit 1), not TXE. State becomes 0x8002.
                // This is what firmware is likely polling for.
                s.isr |= I2C_ISR_TXIS;
            } else {
                handle_tx_completion();
            }
        } else {
             handle_tx_completion();
        }
    }
}

// ------------------ MMIO read ------------------
uint64_t i2c1_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque; (void)size;
    uint32_t off = (uint32_t)(addr - I2C1_BASE);
    uint64_t v = 0;

    switch (off) {
    case I2C_CR1_OFFSET:     v = s.cr1; break;
    case I2C_CR2_OFFSET:     v = s.cr2; break;
    case I2C_OAR1_OFFSET:    v = s.oar1; break;
    case I2C_OAR2_OFFSET:    v = s.oar2; break;
    case I2C_TIMINGR_OFFSET: v = s.timingr; break;
    case I2C_ISR_OFFSET:     v = s.isr; break;

    case I2C_RXDR_OFFSET: {
        v = s.rxdr;
        s.isr &= ~I2C_ISR_RXNE; // Clear RXNE

        if (s.phase == PHASE_XFER_RX) {
            if (s.moved < s.nbytes) {
                // Preload next byte to match HW loop 1/3 (ISR=0x8005)
                s.rxdr = api_i2c_recv(&s.bus);
                s.moved++;
                s.isr |= I2C_ISR_RXNE; // data ready again
                // TXE remains set (as in 0x8005)
            } else {
                // Completed programmed byte count
                if (s.cr2 & I2C_CR2_RELOAD) {
                    s.isr |= I2C_ISR_TCR;
                } else {
                    s.isr |= I2C_ISR_TC;
                    if (s.cr2 & I2C_CR2_AUTOEND) {
                        end_transfer_with_stop();
                    }
                }
            }
        }
        break;
    }

    default:
        dd("[I2C1] Read: unhandled offset");
        v = 0;
        break;
    }
    return v;
}

// ------------------ MMIO write -----------------
void i2c1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque; (void)size;
    uint32_t off = (uint32_t)(addr - I2C1_BASE);
    uint32_t val = (uint32_t)value;

    switch (off) {
    case I2C_CR1_OFFSET:
        s.cr1 = val;
        if ((s.cr1 & I2C_CR1_PE) == 0) {
            i2c_reset_state();
        }
        break;

    case I2C_CR2_OFFSET: {
        uint32_t incoming = val;
        s.cr2 = incoming & ~(I2C_CR2_START | I2C_CR2_STOP);

        if (incoming & I2C_CR2_STOP) {
            end_transfer_with_stop();
        }
        if (incoming & I2C_CR2_START) {
            start_transaction();
        }
        break;
    }

    case I2C_OAR1_OFFSET:    s.oar1 = val; break;
    case I2C_OAR2_OFFSET:    s.oar2 = val; break;
    case I2C_TIMINGR_OFFSET: s.timingr = val; break;

    case I2C_ICR_OFFSET:
        if (val & I2C_ICR_NACKCF) s.isr &= ~I2C_ISR_NACKF;
        if (val & I2C_ICR_STOPCF) s.isr &= ~I2C_ISR_STOPF;
        if (val & I2C_ICR_ADDRCF) s.isr &= ~I2C_ISR_ADDR;
        break;

    case I2C_TXDR_OFFSET:
        s.txdr = (uint8_t)val;
        // Writing TXDR clears both TXE and TXIS
        s.isr &= ~(I2C_ISR_TXE | I2C_ISR_TXIS);

        if (s.phase == PHASE_XFER_TX) {
            // This is a subsequent byte write *during* a transfer
            int rc = api_i2c_send(&s.bus, s.txdr);
            s.moved++;

            if (rc != 0) {
                s.isr |= I2C_ISR_NACKF;
            }

            if (s.moved < s.nbytes) {
                // **FIX**: Set TXIS (bit 1), not TXE. State becomes 0x8002.
                s.isr |= I2C_ISR_TXIS;
            } else {
                handle_tx_completion();
            }
        }
        // If s.phase is PHASE_IDLE, this was a preload.
        // TXE and TXIS remain 0 until START.
        break;

    default:
        dd("[I2C1] Write: unhandled offset");
        break;
    }
}

// ------------------ Initialization -----------------
void i2c1_init(ConfigSection* model_info) {
    memset(&s, 0, sizeof(s));
    s.bus = api_i2c_init_bus(model_info);
    i2c_reset_state();

    s.oar1 = 0x00000000;
    s.oar2 = 0x00000000;
    dd("[I2C1] init: reset to TXE=1 (0x1).");
    dd("[I2C1] Note: TX-active poll state is now BUSY|TXIS (0x8002).");
}