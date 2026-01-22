// Device Model for I2C1 (STM32F103xx)
//
// Inferred Register Functions:
//   CR1   : PE/START/STOP/ACK/SWRST control
//   CR2   : timing/config
//   OAR1/2: own address config
//   DR    : address/data TX and data RX
//   SR1   : SB/ADDR/BTF/RXNE/TXE status (polled by firmware)
//   SR2   : MSL/BUSY/TRA status (read clears ADDR after SR1 read)
//   CCR/TRISE: timing
//
// Key behaviors matched to trace:
//   - START: SR1.SB=1 (0x01)
//   - Address byte written to DR: SR1 becomes 0x86 (ADDR|TXE|BTF)
//   - Reading SR2 after SR1 read clears ADDR and enters TX or RX phase
//   - TX phase: SR1 0x84 (TXE|BTF), api_i2c_send on DR writes
//   - RX phase: SR1 0x44 (RXNE|BTF), api_i2c_recv provides DR reads
//   - STOP in RX is DEFERRED until the staged RX byte is consumed via DR read
//     (fixes SR2 spinning at 0x3 BUSY after single-byte receive)

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define I2C1_BASE 0x40005400u

#define REG_CR1    0x00
#define REG_CR2    0x04
#define REG_OAR1   0x08
#define REG_OAR2   0x0C
#define REG_DR     0x10
#define REG_SR1    0x14
#define REG_SR2    0x18
#define REG_CCR    0x1C
#define REG_TRISE  0x20

// CR1 bits
#define CR1_PE      (1u << 0)
#define CR1_START   (1u << 8)
#define CR1_STOP    (1u << 9)
#define CR1_ACK     (1u << 10)
#define CR1_SWRST   (1u << 15)

// SR1 bits (subset)
#define SR1_SB      (1u << 0)
#define SR1_ADDR    (1u << 1)
#define SR1_BTF     (1u << 2)
#define SR1_RXNE    (1u << 6)
#define SR1_TXE     (1u << 7)
#define SR1_AF      (1u << 10)

// SR2 bits (subset)
#define SR2_MSL     (1u << 0)
#define SR2_BUSY    (1u << 1)
#define SR2_TRA     (1u << 2)

typedef enum {
    ST_IDLE = 0,
    ST_START_SENT,
    ST_ADDR_SENT,
    ST_TX,
    ST_RX,
} i2c_state_e;

typedef struct {
    // visible regs
    uint32_t CR1, CR2, OAR1, OAR2, CCR, TRISE;
    uint8_t  DR;
    uint32_t SR1, SR2;

    // backend bus
    I2CBus bus;
    bool   bus_inited;

    // internal state
    i2c_state_e st;
    bool xfer_active;
    bool is_recv;
    uint8_t addr7;

    // ADDR clear handshake tracking
    bool pending_addr_clear;

    // RX staging
    bool    rx_valid;
    uint8_t rx_byte;

    // STOP deferral for RX
    bool stop_pending;
} i2c1_state_t;

static i2c1_state_t g_i2c1;

static inline uint32_t i2c_offs(hwaddr addr) {
    uint64_t a = (uint64_t)addr;
    if (a >= (uint64_t)I2C1_BASE) return (uint32_t)(a - (uint64_t)I2C1_BASE);
    return (uint32_t)a;
}

static void i2c1_reset(i2c1_state_t *s) {
    s->CR1 = 0;
    s->CR2 = 0;
    s->OAR1 = 0;
    s->OAR2 = 0;
    s->CCR = 0;
    s->TRISE = 0x2;

    s->DR = 0;
    s->SR1 = 0;
    s->SR2 = 0;

    s->st = ST_IDLE;
    s->xfer_active = false;
    s->is_recv = false;
    s->addr7 = 0;

    s->pending_addr_clear = false;

    s->rx_valid = false;
    s->rx_byte = 0;

    s->stop_pending = false;
}

static void i2c1_end_transfer(i2c1_state_t *s) {
    if (s->xfer_active) {
        api_i2c_end_transfer(&s->bus);
    }

    s->xfer_active = false;
    s->is_recv = false;
    s->addr7 = 0;

    s->pending_addr_clear = false;

    s->rx_valid = false;
    s->stop_pending = false;

    // clear master/busy/tra and primary status bits we synthesize
    s->SR2 &= ~(SR2_MSL | SR2_BUSY | SR2_TRA);
    s->SR1 &= ~(SR1_SB | SR1_ADDR | SR1_RXNE | SR1_TXE | SR1_BTF | SR1_AF);

    s->st = ST_IDLE;
}

static void i2c1_issue_start(i2c1_state_t *s) {
    // Treat repeated-start as a fresh selection at backend layer.
    if (s->xfer_active) {
        api_i2c_end_transfer(&s->bus);
        s->xfer_active = false;
    }

    s->stop_pending = false;

    s->st = ST_START_SENT;

    s->SR2 |= (SR2_MSL | SR2_BUSY);
    s->SR1 |= SR1_SB;

    s->SR1 &= ~(SR1_ADDR | SR1_RXNE | SR1_TXE | SR1_BTF | SR1_AF);
    s->pending_addr_clear = false;

    s->rx_valid = false;
}

static void i2c1_enter_data_phase(i2c1_state_t *s) {
    // clear ADDR once SR1 then SR2 read sequence occurs
    s->SR1 &= ~SR1_ADDR;
    s->pending_addr_clear = false;

    if (!s->xfer_active) {
        s->st = ST_IDLE;
        return;
    }

    if (s->is_recv) {
        s->st = ST_RX;
        s->SR2 &= ~SR2_TRA;

        // Prefetch 1 byte so firmware sees SR1=0x44 then reads DR
        uint8_t b = api_i2c_recv(&s->bus);
        s->rx_byte = b;
        s->rx_valid = true;
        s->DR = b;

        s->SR1 |= (SR1_RXNE | SR1_BTF);
        s->SR1 &= ~SR1_TXE;
    } else {
        s->st = ST_TX;
        s->SR2 |= SR2_TRA;

        s->SR1 |= (SR1_TXE | SR1_BTF);
        s->SR1 &= ~SR1_RXNE;
    }
}

static void i2c1_handle_address_byte(i2c1_state_t *s, uint8_t addr_byte) {
    s->addr7 = (uint8_t)((addr_byte >> 1) & 0x7Fu);
    s->is_recv = ((addr_byte & 0x01u) != 0);

    int rc = api_i2c_start_transfer(&s->bus, s->addr7, s->is_recv);
    if (rc != 0) {
        // NACK from backend: set AF, stay in start state
        s->SR1 |= SR1_AF;
        s->xfer_active = false;
        s->st = ST_START_SENT;
        return;
    }

    s->xfer_active = true;
    s->st = ST_ADDR_SENT;

    // ADDR visible until SR1 then SR2 read
    s->SR1 |= SR1_ADDR;

    // Address-phase in trace reads as 0x86 (ADDR|TXE|BTF)
    s->SR1 |= (SR1_TXE | SR1_BTF);
    s->SR1 &= ~SR1_RXNE;

    if (s->is_recv) s->SR2 &= ~SR2_TRA;
    else s->SR2 |= SR2_TRA;

    s->pending_addr_clear = false;
}

static uint16_t i2c1_sr1_dynamic(i2c1_state_t *s) {
    uint32_t sr1 = s->SR1;

    if (s->xfer_active) {
        if (s->st == ST_RX && s->is_recv) {
            // Ensure a byte is staged; keep SR1 at 0x44 while data pending/ready.
            if (!s->rx_valid) {
                uint8_t b = api_i2c_recv(&s->bus);
                s->rx_byte = b;
                s->rx_valid = true;
                s->DR = b;
            }
            sr1 |= (SR1_RXNE | SR1_BTF);
            sr1 &= ~SR1_TXE;
            s->SR1 = sr1;
        } else if (s->st == ST_TX && !s->is_recv) {
            sr1 |= (SR1_TXE | SR1_BTF);
            sr1 &= ~SR1_RXNE;
            s->SR1 = sr1;
        }
    }

    return (uint16_t)(sr1 & 0xFFFFu);
}

static uint16_t i2c1_sr2_dynamic(i2c1_state_t *s) {
    uint32_t sr2 = s->SR2;
    if (s->xfer_active) {
        sr2 |= (SR2_MSL | SR2_BUSY);
        if (s->is_recv) sr2 &= ~SR2_TRA;
        else sr2 |= SR2_TRA;
    }
    return (uint16_t)(sr2 & 0xFFFFu);
}

// This function will emulation all device reads
uint64_t i2c1_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque;
    i2c1_state_t *s = &g_i2c1;

    uint32_t off = i2c_offs(addr);
    uint32_t val = 0;

    switch (off) {
        case REG_CR1:   val = s->CR1; break;
        case REG_CR2:   val = s->CR2; break;
        case REG_OAR1:  val = s->OAR1; break;
        case REG_OAR2:  val = s->OAR2; break;
        case REG_CCR:   val = s->CCR; break;
        case REG_TRISE: val = s->TRISE; break;

        case REG_SR1: {
            uint16_t sr1 = i2c1_sr1_dynamic(s);
            val = sr1;

            // Track SR1->SR2 read ordering for ADDR clear
            if (sr1 & SR1_ADDR) {
                s->pending_addr_clear = true;
            }
            break;
        }

        case REG_SR2: {
            uint16_t sr2 = i2c1_sr2_dynamic(s);
            val = sr2;

            // SR2 read clears ADDR if SR1 was read first
            if (s->pending_addr_clear) {
                i2c1_enter_data_phase(s);
            }
            break;
        }

        case REG_DR: {
            if (s->rx_valid) {
                val = (uint32_t)s->rx_byte;
                s->rx_valid = false;

                // After consuming the staged byte, clear RX flags
                s->SR1 &= ~(SR1_RXNE | SR1_BTF);

                // If STOP was requested for RX, end transfer *after* the byte is consumed
                // (fixes firmware looping on SR2 BUSY==1).
                if (s->stop_pending) {
                    // For the single-byte path, ACK is typically 0 when STOP is asserted.
                    if ((s->CR1 & CR1_ACK) == 0) {
                        i2c1_end_transfer(s);
                    } else {
                        // If ACK is still set, treat as multi-byte and keep going.
                        // Stage next byte immediately so SR1 becomes 0x44 again.
                        uint8_t b = api_i2c_recv(&s->bus);
                        s->rx_byte = b;
                        s->rx_valid = true;
                        s->DR = b;
                        s->SR1 |= (SR1_RXNE | SR1_BTF);
                        s->SR1 &= ~SR1_TXE;
                    }
                } else {
                    // Normal multi-byte receive: if ACK=1, stage next byte immediately
                    if (s->xfer_active && s->is_recv && (s->CR1 & CR1_ACK)) {
                        uint8_t b = api_i2c_recv(&s->bus);
                        s->rx_byte = b;
                        s->rx_valid = true;
                        s->DR = b;
                        s->SR1 |= (SR1_RXNE | SR1_BTF);
                        s->SR1 &= ~SR1_TXE;
                    }
                }
            } else {
                val = (uint32_t)s->DR;
            }
            break;
        }

        default: {
            char buf[128];
            snprintf(buf, sizeof(buf), "[i2c1] Unhandled READ off=0x%02x size=%u\n", off, size);
            dev_debug(buf);
            val = 0;
            break;
        }
    }

    if (size == 1) return (uint64_t)(val & 0xFFu);
    if (size == 2) return (uint64_t)(val & 0xFFFFu);
    return (uint64_t)(val & 0xFFFFu);
}

// This function will emulate all device writes
void i2c1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque;
    (void)size;
    i2c1_state_t *s = &g_i2c1;

    uint32_t off = i2c_offs(addr);
    uint32_t v16 = (uint32_t)(value & 0xFFFFu);

    switch (off) {
        case REG_CR1: {
            uint32_t old = s->CR1;
            s->CR1 = (v16 & 0xFFFFu);

            // software reset
            if (s->CR1 & CR1_SWRST) {
                bool keep_bus = s->bus_inited;
                I2CBus keep = s->bus;
                i2c1_reset(s);
                s->bus_inited = keep_bus;
                s->bus = keep;
                s->CR1 = (v16 & 0xFFFFu);
                break;
            }

            // PE falling edge -> end transfer
            if (!(s->CR1 & CR1_PE) && (old & CR1_PE)) {
                i2c1_end_transfer(s);
            }

            // START rising edge
            if ((v16 & CR1_START) && !(old & CR1_START)) {
                i2c1_issue_start(s);
            }

            // STOP rising edge
            if ((v16 & CR1_STOP) && !(old & CR1_STOP)) {
                // Defer STOP during RX until DR read consumes staged byte
                if (s->xfer_active && s->is_recv) {
                    s->stop_pending = true;

                    // Keep RX flags asserted if we already staged a byte
                    if (s->st == ST_RX && s->rx_valid) {
                        s->SR1 |= (SR1_RXNE | SR1_BTF);
                        s->SR1 &= ~SR1_TXE;
                    }
                } else {
                    i2c1_end_transfer(s);
                }
            }

            // self-clearing START/STOP bits in emulation
            s->CR1 &= ~(CR1_START | CR1_STOP);
            break;
        }

        case REG_CR2:   s->CR2 = (v16 & 0xFFFFu); break;
        case REG_OAR1:  s->OAR1 = (v16 & 0xFFFFu); break;
        case REG_OAR2:  s->OAR2 = (v16 & 0xFFFFu); break;
        case REG_CCR:   s->CCR = (v16 & 0xFFFFu); break;
        case REG_TRISE: s->TRISE = (v16 & 0xFFFFu); break;

        case REG_DR: {
            uint8_t b = (uint8_t)(value & 0xFFu);
            s->DR = b;

            // Address byte right after START
            if (s->st == ST_START_SENT || (s->SR1 & SR1_SB)) {
                s->SR1 &= ~SR1_SB;
                i2c1_handle_address_byte(s, b);
                break;
            }

            // If firmware writes data before clearing ADDR, be forgiving
            if ((s->SR1 & SR1_ADDR) && s->xfer_active) {
                i2c1_enter_data_phase(s);
            }

            // TX data phase
            if (s->xfer_active && !s->is_recv) {
                s->SR1 &= ~(SR1_TXE | SR1_BTF);

                int ack = api_i2c_send(&s->bus, b);
                if (ack != 0) s->SR1 |= SR1_AF;
                else s->SR1 &= ~SR1_AF;

                // ready for next byte: 0x84
                s->SR1 |= (SR1_TXE | SR1_BTF);
                s->st = ST_TX;
            }
            break;
        }

        // allow W1C-like clearing (coarse)
        case REG_SR1: {
            uint32_t w = (uint32_t)(value & 0xFFFFu);
            uint32_t clear_mask = w & 0xFF00u;
            s->SR1 &= ~clear_mask;
            break;
        }

        default: {
            char buf[140];
            snprintf(buf, sizeof(buf),
                     "[i2c1] Unhandled WRITE off=0x%02x size=%u val=0x%llx\n",
                     off, size, (unsigned long long)value);
            dev_debug(buf);
            break;
        }
    }
}

void i2c1_init(ConfigSection* model_info) {
    memset(&g_i2c1, 0, sizeof(g_i2c1));
    i2c1_reset(&g_i2c1);

    g_i2c1.bus = api_i2c_init_bus(model_info);
    g_i2c1.bus_inited = true;

    dev_debug("[i2c1] init done\n");
}
