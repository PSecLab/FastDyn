// Device Model for I2C1
#include <device.h>
#include <devmodels_apis.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ---------------- Base + Offsets ----------------
#define I2C1_BASE        0x40005400u
#define I2C_CR1_OFFSET   0x00u
#define I2C_CR2_OFFSET   0x04u
#define I2C_OAR1_OFFSET  0x08u
#define I2C_OAR2_OFFSET  0x0Cu
#define I2C_DR_OFFSET    0x10u
#define I2C_SR1_OFFSET   0x14u
#define I2C_SR2_OFFSET   0x18u
#define I2C_CCR_OFFSET   0x1Cu
#define I2C_TRISE_OFFSET 0x20u

// ---------------- Bitfields ---------------------
// CR1
#define I2C_CR1_PE       (1u << 0)
#define I2C_CR1_START    (1u << 8)
#define I2C_CR1_STOP     (1u << 9)
#define I2C_CR1_ACK      (1u << 10)
#define I2C_CR1_SWRST    (1u << 15)

// OAR1
#define I2C_OAR1_ADDMODE (1u << 15) // 10-bit address mode

// SR1
#define I2C_SR1_SB       (1u << 0)
#define I2C_SR1_ADDR     (1u << 1)
#define I2C_SR1_BTF      (1u << 2)
#define I2C_SR1_ADD10    (1u << 3)
#define I2C_SR1_RXNE     (1u << 6)
#define I2C_SR1_TXE      (1u << 7)
#define I2C_SR1_AF       (1u << 10)

// SR2
#define I2C_SR2_MSL      (1u << 0)
#define I2C_SR2_BUSY     (1u << 1)
#define I2C_SR2_TRA      (1u << 2)

// ---------------- Internal State ----------------
typedef enum {
    PHASE_IDLE = 0,
    PHASE_START_SENT,        // SR1.SB visible
    PHASE_10BIT_HDR_SENT,    // after first 10-bit header; SR1.ADD10=1
    PHASE_10BIT_ADDR_SET,    // after low byte accepted; SR1.ADDR=1 (until SR2 read)
    PHASE_XFER_TX,
    PHASE_XFER_RX
} i2c_phase_t;

typedef struct {
    // programmer-visible
    uint32_t cr1, cr2, oar1, oar2;
    uint8_t  dr;
    uint32_t sr1, sr2;
    uint32_t ccr, trise;

    // protocol state
    i2c_phase_t phase;
    uint8_t  hdr_first;       // first 10-bit header byte
    uint16_t addr10;          // full 10-bit address once known
    bool     addr10_valid;    // low byte captured
    bool     xfer_is_recv;    // current transfer direction

    // bus
    I2CBus   bus;
} I2C1State;

static I2C1State s;

// ----------- Small helpers for logging ----------
static inline void dbg(const char *msg) {
    dev_debug((char*)msg);
}
static inline bool is_10bit_mode(void) {
    return (s.oar1 & I2C_OAR1_ADDMODE) != 0;
}
static inline uint8_t header_a9a8(uint8_t hdr) { // extract A9..A8
    return (uint8_t)((hdr >> 1) & 0x3);
}

// ---------------- MMIO READ ---------------------
uint64_t i2c1_read(void *opaque, hwaddr addr, unsigned size) {
    uint32_t off = (uint32_t)(addr - I2C1_BASE);
    uint64_t v = 0;

    switch (off) {
    case I2C_CR1_OFFSET:
        v = s.cr1; // START/STOP are not latched
        break;

    case I2C_CR2_OFFSET:
        v = s.cr2;
        break;

    case I2C_OAR1_OFFSET:
        v = s.oar1;
        break;

    case I2C_OAR2_OFFSET:
        v = s.oar2;
        break;

    case I2C_DR_OFFSET:
        // Reading DR returns byte and clears RXNE. In RX mode we prefetch next and assert RXNE|BTF again.
        v = s.dr;
        if (s.sr1 & I2C_SR1_RXNE) {
            s.sr1 &= ~I2C_SR1_RXNE;
            if (s.phase == PHASE_XFER_RX) {
                s.dr = api_i2c_recv(&s.bus);
                s.sr1 |= (I2C_SR1_RXNE | I2C_SR1_BTF); // match hardware pattern 0x44 after DR read
            } else {
                s.sr1 &= ~I2C_SR1_BTF;
            }
        }
        break;

    case I2C_SR1_OFFSET:
        // In TX mode, once TXE is set the next SR1 read may also reflect BTF.
        if ((s.sr2 & I2C_SR2_TRA) && (s.sr1 & I2C_SR1_TXE)) {
            s.sr1 |= I2C_SR1_BTF;
        }
        v = s.sr1;
        break;

    case I2C_SR2_OFFSET:
        v = s.sr2;
        // Reading SR2 clears ADDR; this also transitions into data phase.
        if (s.sr1 & I2C_SR1_ADDR) {
            s.sr1 &= ~I2C_SR1_ADDR;
            if (s.xfer_is_recv) {
                // first RX byte becomes available quickly
                s.dr = api_i2c_recv(&s.bus);
                s.sr1 |= I2C_SR1_RXNE;
                s.phase = PHASE_XFER_RX;
            } else {
                s.sr1 |= I2C_SR1_TXE;
                s.phase = PHASE_XFER_TX;
            }
        }
        break;

    case I2C_CCR_OFFSET:
        v = s.ccr;
        break;

    case I2C_TRISE_OFFSET:
        v = s.trise;
        break;

    default:
        v = 0;
        break;
    }
    return v;
}

// ---------------- MMIO WRITE --------------------
void i2c1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    uint32_t off = (uint32_t)(addr - I2C1_BASE);

    switch (off) {
    case I2C_CR1_OFFSET: {
        uint32_t val = (uint32_t)value;

        // START/STOP are actions, not latched
        if (val & I2C_CR1_START) {
            s.sr1 |= I2C_SR1_SB;
            s.sr2 |= (I2C_SR2_MSL | I2C_SR2_BUSY);
            s.phase = PHASE_START_SENT;
            dbg("[I2C1] START\n");
        }
        if (val & I2C_CR1_STOP) {
            api_i2c_end_transfer(&s.bus);
            s.sr2 &= ~(I2C_SR2_MSL | I2C_SR2_BUSY | I2C_SR2_TRA);
            s.sr1 = 0;
            if (val & I2C_CR1_PE) s.sr1 |= I2C_SR1_TXE;
            s.phase = PHASE_IDLE;
            dbg("[I2C1] STOP\n");
        }

        // Latch persistent bits
        s.cr1 = val & ~(I2C_CR1_START | I2C_CR1_STOP);

        // If disabled, clear status
        if ((s.cr1 & I2C_CR1_PE) == 0) {
            s.sr1 = 0;
            s.sr2 = 0;
            s.phase = PHASE_IDLE;
            s.addr10_valid = false;
            dbg("[I2C1] PE=0, reset status\n");
        }
        break;
    }

    case I2C_CR2_OFFSET:
        s.cr2 = (uint32_t)value;
        break;

    case I2C_OAR1_OFFSET:
        // RM: bit14 must remain 1
        s.oar1 = ((uint32_t)value) | (1u << 14);
        break;

    case I2C_OAR2_OFFSET:
        s.oar2 = (uint32_t)value;
        break;

    case I2C_DR_OFFSET: {
        uint8_t byte = (uint8_t)value;
        s.dr = byte;

        // ----- Address phase handling -----
        if (s.sr1 & I2C_SR1_SB) {
            // First byte after START is always an address header
            s.sr1 &= ~I2C_SR1_SB;

            if (is_10bit_mode()) {
                // 10-bit address header (11110 A9 A8 R/W)
                s.hdr_first = byte;

                // If we already have the low byte (addr10_valid) and we see a repeated-START header with R/W=1
                // that matches A9..A8, this is the read header: directly raise ADDR (no ADD10 this time).
                if (s.addr10_valid && ((byte & 0x1) == 1) &&
                    (header_a9a8(byte) == ((s.addr10 >> 8) & 0x3))) {
                    // Begin 10-bit read
                    s.xfer_is_recv = true;
                    // Call start with the full 10-bit address (read)
                    if (api_i2c_start_transfer_10bit(&s.bus, s.addr10, true) == 0) {
                        s.sr2 &= ~I2C_SR2_TRA;  // receiver
                        s.sr1 |= I2C_SR1_ADDR; // hardware sets ADDR here
                        s.phase = PHASE_10BIT_ADDR_SET;
                        dbg("[I2C1] 10-bit Repeated-START READ header accepted → ADDR\n");
                    } else {
                        s.sr1 |= I2C_SR1_AF;
                        dbg("[I2C1] 10-bit Repeated-START READ header NACK → AF\n");
                    }
                } else {
                    // First phase: header → expect low byte next; hardware raises ADD10
                    s.sr1 |= I2C_SR1_ADD10;
                    s.phase = PHASE_10BIT_HDR_SENT;
                    dbg("[I2C1] 10-bit header written → ADD10\n");
                }
            } else {
                // 7-bit addressing: immediate start_transfer
                uint8_t addr7   = (uint8_t)(byte >> 1);
                bool is_recv    = (byte & 0x1) != 0;
                int rc = api_i2c_start_transfer(&s.bus, addr7, is_recv);
                if (rc == 0) {
                    s.xfer_is_recv = is_recv;
                    if (is_recv) s.sr2 &= ~I2C_SR2_TRA; else s.sr2 |= I2C_SR2_TRA;
                    s.sr1 |= I2C_SR1_ADDR;
                    s.phase = PHASE_10BIT_ADDR_SET; // reuse state name for "ADDR pending"
                    dbg("[I2C1] 7-bit address accepted → ADDR\n");
                } else {
                    s.sr1 |= I2C_SR1_AF;
                    dbg("[I2C1] 7-bit address NACK → AF\n");
                }
            }
            break;
        }

        if (s.sr1 & I2C_SR1_ADD10) {
            // Second byte of 10-bit address (low 8 bits)
            s.sr1 &= ~I2C_SR1_ADD10;

            uint16_t a9a8 = (uint16_t)header_a9a8(s.hdr_first);
            s.addr10 = (uint16_t)((a9a8 << 8) | byte);
            s.addr10_valid = true;

            // Per RM, first phase is always "write" (R/W=0). Slave is selected after this byte -> ADDR set.
            // We initiate a write target selection; the actual read will happen after a repeated START.
            int rc = api_i2c_start_transfer_10bit(&s.bus, s.addr10, false /*select in write phase*/);
            if (rc == 0) {
                s.xfer_is_recv = false;
                s.sr2 |= I2C_SR2_TRA;    // transmitter during the selection phase
                s.sr1 |= I2C_SR1_ADDR;   // hardware shows ADDR after low byte
                s.phase = PHASE_10BIT_ADDR_SET;
                dbg("[I2C1] 10-bit low byte accepted → ADDR\n");
            } else {
                s.sr1 |= I2C_SR1_AF;
                dbg("[I2C1] 10-bit low byte NACK → AF\n");
            }
            break;
        }

        // ----- Data phase -----
        if (s.sr2 & I2C_SR2_TRA) {
            // Master-Transmit: write data to slave
            s.sr1 &= ~(I2C_SR1_TXE | I2C_SR1_BTF);
            if (api_i2c_send(&s.bus, byte) != 0) {
                s.sr1 |= I2C_SR1_AF;
            }
            s.sr1 |= I2C_SR1_TXE; // BTF will appear on SR1 read
            s.phase = PHASE_XFER_TX;
        } else {
            // Master-Receive but CPU wrote DR (unusual). Keep status sane; no state change.
        }
        break;
    }

    case I2C_CCR_OFFSET:
        s.ccr = (uint32_t)value;
        break;

    case I2C_TRISE_OFFSET:
        s.trise = (uint32_t)value;
        break;

    default:
        break;
    }
}

// ---------------- Initialization ----------------
void i2c1_init(void *opaque) {
    memset(&s, 0, sizeof(s));
    s.bus = api_i2c_init_bus((ConfigSection*)opaque);

    // Reset-like defaults (consistent with traces)
    s.trise = 0x2;
    s.oar1  = (1u << 14);   // bit14 kept at 1 on STM32
    s.sr1   = 0;
    s.sr2   = 0;
    s.phase = PHASE_IDLE;
    s.addr10_valid = false;

    dbg("[I2C1] Init complete\n");
}

/*
Inferred Register Functions:
- CR1: START sets SR1.SB and SR2.MSL|BUSY; STOP clears bus state and ends transfer. START/STOP are not latched.
- DR: First write after SB is address header (7-bit or 10-bit). In RX, reading DR clears RXNE and we prefetch next byte (then assert RXNE|BTF).
- SR1: SB, ADD10, ADDR, RXNE, TXE, BTF, AF modeled. Reading SR2 clears ADDR.
- SR2: MSL/BUSY/TRA modeled; TRA mirrors direction (1=Master-Transmit).
- CCR/TRISE/CR2/OARx: Latched only.
*/
