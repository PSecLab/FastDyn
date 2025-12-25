#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "device.h"
#include "devmodels_apis.h"

/*
 * MAX78000_FTHR GPIO2 model
 *
 * This model is built ONLY from the provided MMIO trace summary.
 * The key fixes compared to the previous (mismatched) model are:
 *  1. Correct base address: 0x40080400
 *  2. Correct reset/default values for:
 *        VSSEL = 0xC6     (hardware read)
 *        INEN  = 0xFF     (hardware read)
 *
 * With these, the emulated init sequence will match:
 *   - WRITE OUT_SET (0x4008041C) = 0x1
 *   - READ VSSEL (0x400804C0)    = 0xC6
 *   - WRITE VSSEL (0x400804C0)   = 0xC7
 *   - READ INEN (0x40080430)     = 0xFF
 *   - WRITE INEN (0x40080430)    = 0xFF
 */

/* Base address derived from trace:
 *  OUT_SET seen at 0x4008041C, offset is 0x1C → base = 0x40080400
 */
#define GPIO2_BASE       0x40080400UL

/* Register offsets (inferred from trace) */
#define GPIO2_EN0        0x000   /* R/W */
#define GPIO2_EN0_SET    0x004   /* W: set bits in EN0 */
#define GPIO2_OUTEN      0x00C   /* R/W */
#define GPIO2_OUTEN_SET  0x010   /* W: set bits in OUTEN */
#define GPIO2_OUT        0x018   /* R/W */
#define GPIO2_OUT_SET    0x01C   /* W: set bits in OUT */
#define GPIO2_IN         0x024   /* R   */
#define GPIO2_INEN       0x030   /* R/W */
#define GPIO2_EN1        0x06C   /* R/W */
#define GPIO2_EN1_CLR    0x070   /* W: clear bits in EN1 */
#define GPIO2_PADCTRL0   0x0B0   /* R/W */
#define GPIO2_PADCTRL1   0x0B4   /* R/W */
#define GPIO2_VSSEL      0x0C0   /* R/W */
#define GPIO2_DS0        0x0C8   /* R/W */
#define GPIO2_DS1        0x0CC   /* R/W */

/* Device state */
typedef struct {
    uint32_t en0;        /* 0x000 */
    uint32_t outen;      /* 0x00C */
    uint32_t out;        /* 0x018 */
    uint32_t inen;       /* 0x030 */
    uint32_t en1;        /* 0x06C */
    uint32_t padctrl0;   /* 0x0B0 */
    uint32_t padctrl1;   /* 0x0B4 */
    uint32_t vssel;      /* 0x0C0 */
    uint32_t ds0;        /* 0x0C8 */
    uint32_t ds1;        /* 0x0CC */
    /* NOTE: IN (0x024) is modeled as read-only and sourced from “external pins”.
       Since we have no external stimulus in the provided data, we return 0. */
} gpio2_state_t;

static gpio2_state_t gpio2_state;

/* Utility: size-based masking on read */
static uint64_t gpio2_mask_value(uint32_t val, unsigned size, hwaddr addr)
{
    /* For little-endian MMIO, sub-word reads from a 32-bit reg should give the
       corresponding bytes. We implement the common case only. */
    switch (size) {
    case 1: {
        /* Pick the byte based on addr offset inside the word */
        unsigned shift = (addr & 0x3) * 8;
        return (val >> shift) & 0xFFU;
    }
    case 2: {
        unsigned shift = (addr & 0x2) * 8;
        return (val >> shift) & 0xFFFFU;
    }
    default:
        return val;
    }
}

/* Device Model for GPIO2 -------------------------------------------------- */

/* This function will emulate all device reads */
uint64_t gpio2_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t offset = (uint32_t)(addr - GPIO2_BASE);
    uint32_t val = 0;
    char msg[128];

    switch (offset) {
    case GPIO2_EN0:
        val = gpio2_state.en0;
        break;
    case GPIO2_OUTEN:
        val = gpio2_state.outen;
        break;
    case GPIO2_OUT:
        val = gpio2_state.out;
        break;
    case GPIO2_IN:
        /* No external data provided → return 0. If later we get a source,
           we can wire it here. */
        val = 0x00000000U;
        break;
    case GPIO2_INEN:
        val = gpio2_state.inen;
        break;
    case GPIO2_EN1:
        val = gpio2_state.en1;
        break;
    case GPIO2_PADCTRL0:
        val = gpio2_state.padctrl0;
        break;
    case GPIO2_PADCTRL1:
        val = gpio2_state.padctrl1;
        break;
    case GPIO2_VSSEL:
        /* This is where the earlier model mismatched (was 0x0, should be 0xC6). */
        val = gpio2_state.vssel;
        break;
    case GPIO2_DS0:
        val = gpio2_state.ds0;
        break;
    case GPIO2_DS1:
        val = gpio2_state.ds1;
        break;
    default:
        /* Unhandled read – log it */
        snprintf(msg, sizeof(msg),
                 "GPIO2: Unhandled READ at 0x%08lx (offset 0x%03x), size %u",
                 (unsigned long)addr, offset, size);
        dev_debug(msg);
        val = 0;
        break;
    }

    return gpio2_mask_value(val, size, addr);
}

/* This function will emulate all device writes */
void gpio2_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    uint32_t offset = (uint32_t)(addr - GPIO2_BASE);
    uint32_t wval = (uint32_t)value;
    char msg[160];

    /* All writes in the provided trace are 32-bit, so we mainly implement that path. */

    switch (offset) {
    /* --- write-only “set/clear” style registers --- */
    case GPIO2_OUT_SET:
        /* set output bits */
        gpio2_state.out |= wval;
        snprintf(msg, sizeof(msg),
                 "GPIO2: OUT_SET=0x%08x → OUT now 0x%08x", wval, gpio2_state.out);
        dev_debug(msg);
        break;

    case GPIO2_EN0_SET:
        gpio2_state.en0 |= wval;
        /* firmware keeps writing 0x1 here in loop_pattern_1/2/3 */
        break;

    case GPIO2_OUTEN_SET:
        gpio2_state.outen |= wval;
        break;

    case GPIO2_EN1_CLR:
        /* clear bits in EN1 */
        gpio2_state.en1 &= ~wval;
        break;

    /* --- direct/stateful registers (RMW patterns) --- */
    case GPIO2_VSSEL:
        /* hardware: READ 0xC6 → WRITE 0xC7 */
        gpio2_state.vssel = wval;
        break;

    case GPIO2_INEN:
        /* hardware: READ 0xFF → WRITE 0xFF */
        gpio2_state.inen = wval;
        break;

    case GPIO2_PADCTRL0:
        gpio2_state.padctrl0 = wval;
        break;

    case GPIO2_PADCTRL1:
        gpio2_state.padctrl1 = wval;
        break;

    case GPIO2_DS0:
        gpio2_state.ds0 = wval;
        break;

    case GPIO2_DS1:
        gpio2_state.ds1 = wval;
        break;

    /* --- base registers that might be written directly --- */
    case GPIO2_EN0:
        gpio2_state.en0 = wval;
        break;

    case GPIO2_OUTEN:
        gpio2_state.outen = wval;
        break;

    case GPIO2_OUT:
        gpio2_state.out = wval;
        break;

    case GPIO2_EN1:
        gpio2_state.en1 = wval;
        break;

    default:
        snprintf(msg, sizeof(msg),
                 "GPIO2: Unhandled WRITE at 0x%08lx (offset 0x%03x) = 0x%08lx (size %u)",
                 (unsigned long)addr, offset, (unsigned long)value, size);
        dev_debug(msg);
        break;
    }
}

/* Initialization entry point for the model */
void gpio2_init(ConfigSection* model_info)
{
    (void)model_info; /* unused for now */

    memset(&gpio2_state, 0, sizeof(gpio2_state));

    /* Fixing the discrepancies observed in the provided “init” traces: */
    gpio2_state.vssel = 0xC6;  /* Hardware read: 0xC6 */
    gpio2_state.inen  = 0xFF;  /* Hardware read: 0xFF */

    /* Everything else can sensibly start at 0. If later traces show a non-zero
       reset for EN1 or padctrl, we can update here. */

    dev_debug("GPIO2: model initialized with VSSEL=0xC6, INEN=0xFF, base=0x40080400");
}
