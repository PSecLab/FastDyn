#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define DCDC_BASE               0x40080000ULL

#define DCDC_REG0_OFFSET        0x00
#define DCDC_REG1_OFFSET        0x04
#define DCDC_REG2_OFFSET        0x08
#define DCDC_REG3_OFFSET        0x0C

/*
 * i.MX RT10xx DCDC:
 * - REG3.TRG is the voltage target trim field in bits [4:0].
 * - REG0.STS_DC_OK is the "converter settled / OK" status bit.
 *
 * The Zephyr clock_init() path only adjusts REG3 then polls REG0.STS_DC_OK.
 * Model that behavior statefully and keep the rest of the block as plain
 * readable/writable registers.
 */
#define DCDC_REG3_TRG_MASK          0x0000001FU
#define DCDC_REG0_STS_DC_OK_MASK    0x80000000U

typedef struct {
    uint32_t reg0_shadow;
    uint32_t reg1;
    uint32_t reg2;
    uint32_t reg3;
    bool dc_ok;
} DCDCState;

static DCDCState g_dcdc;

static uint64_t dcdc_access_mask(unsigned size)
{
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    case 4:
        return 0xFFFFFFFFU;
    default:
        return 0U;
    }
}

static bool dcdc_valid_access(uint64_t offset, unsigned size)
{
    if (size != 1 && size != 2 && size != 4) {
        return false;
    }

    return ((offset & 0x3ULL) + size) <= 4U;
}

static uint32_t dcdc_reg0_value(const DCDCState *s)
{
    uint32_t value = s->reg0_shadow & ~DCDC_REG0_STS_DC_OK_MASK;

    if (s->dc_ok) {
        value |= DCDC_REG0_STS_DC_OK_MASK;
    }

    return value;
}

static uint32_t dcdc_read_reg32(DCDCState *s, uint64_t reg_offset)
{
    switch (reg_offset) {
    case DCDC_REG0_OFFSET:
        return dcdc_reg0_value(s);
    case DCDC_REG1_OFFSET:
        return s->reg1;
    case DCDC_REG2_OFFSET:
        return s->reg2;
    case DCDC_REG3_OFFSET:
        return s->reg3;
    default:
        return 0;
    }
}

static void dcdc_write_reg32(DCDCState *s, uint64_t reg_offset, uint32_t value)
{
    switch (reg_offset) {
    case DCDC_REG0_OFFSET:
        /* Preserve software-controlled bits, but keep STS_DC_OK read-only. */
        s->reg0_shadow = value & ~DCDC_REG0_STS_DC_OK_MASK;
        break;
    case DCDC_REG1_OFFSET:
        s->reg1 = value;
        break;
    case DCDC_REG2_OFFSET:
        s->reg2 = value;
        break;
    case DCDC_REG3_OFFSET:
        /*
         * Firmware trims REG3.TRG then waits for REG0.STS_DC_OK.
         * In hardware the converter is already up during early boot; expose the
         * "OK" status immediately so tight polling loops can make forward progress.
         */
        if (((s->reg3 ^ value) & DCDC_REG3_TRG_MASK) != 0U) {
            s->dc_ok = true;
        }
        s->reg3 = value;
        break;
    default:
        break;
    }
}

void* dcdc_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_dcdc, 0, sizeof(g_dcdc));

    /*
     * The SoC is already running from a valid core supply when this model is
     * instantiated, so expose DC_OK as asserted from reset.
     */
    g_dcdc.dc_ok = true;

    return &g_dcdc;
}

uint64_t dcdc_read(void *opaque, uint64_t addr, unsigned size) {
    DCDCState *s = opaque ? (DCDCState *)opaque : &g_dcdc;
    uint64_t offset;
    uint64_t reg_offset;
    uint32_t reg_value;
    unsigned shift;

    if (addr < DCDC_BASE) {
        return 0;
    }

    offset = addr - DCDC_BASE;
    if (!dcdc_valid_access(offset, size)) {
        return 0;
    }

    reg_offset = offset & ~0x3ULL;
    reg_value = dcdc_read_reg32(s, reg_offset);
    shift = (unsigned)((offset & 0x3ULL) * 8ULL);

    return (reg_value >> shift) & dcdc_access_mask(size);
}

void dcdc_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    DCDCState *s = opaque ? (DCDCState *)opaque : &g_dcdc;
    uint64_t offset;
    uint64_t reg_offset;
    uint64_t mask;
    unsigned shift;
    uint32_t current;
    uint32_t merged;

    if (addr < DCDC_BASE) {
        return;
    }

    offset = addr - DCDC_BASE;
    if (!dcdc_valid_access(offset, size)) {
        return;
    }

    reg_offset = offset & ~0x3ULL;
    mask = dcdc_access_mask(size);
    shift = (unsigned)((offset & 0x3ULL) * 8ULL);

    current = dcdc_read_reg32(s, reg_offset);
    merged = (current & ~((uint32_t)(mask << shift))) |
             (uint32_t)((value & mask) << shift);

    dcdc_write_reg32(s, reg_offset, merged);
}
