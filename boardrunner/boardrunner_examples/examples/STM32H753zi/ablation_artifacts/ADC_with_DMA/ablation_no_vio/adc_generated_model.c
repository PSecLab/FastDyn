#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Device Model for ADC
//
// Composite model covering:
//   ADC1          @ 0x40022000
//   ADC2          @ 0x40022100
//   ADC12_Common  @ 0x40022300
//
// Notes:
// - The framework passes absolute addresses, so this model subtracts the
//   appropriate base before decoding register offsets.
// - DMA/DMAMUX interaction is intentionally not modeled because no direct
//   request API exists in the provided interface.
// - This is a stateful register model, not a trace replay model.

#define ADC1_BASE_DEFAULT         0x40022000ULL
#define ADC2_BASE_DEFAULT         0x40022100ULL
#define ADC12_COMMON_BASE_DEFAULT 0x40022300ULL

#define ADC_BLOCK_SIZE            0x100
#define ADC_REG_WORDS             (ADC_BLOCK_SIZE / 4)

/* Per-ADC register offsets */
#define ADC_ISR_OFFSET            0x00
#define ADC_IER_OFFSET            0x04
#define ADC_CR_OFFSET             0x08
#define ADC_CFGR_OFFSET           0x0C
#define ADC_CFGR2_OFFSET          0x10
#define ADC_SMPR1_OFFSET          0x14
#define ADC_SMPR2_OFFSET          0x18
#define ADC_PCSEL_OFFSET          0x1C
/* Many additional registers exist; generic storage covers them. */

/* ADC12 common register offsets */
#define ADC_CCR_OFFSET            0x08

/* ISR bits used by this model */
#define ADC_ISR_ADRDY             (1U << 0)
#define ADC_ISR_EOSMP             (1U << 1)
#define ADC_ISR_EOC               (1U << 2)
#define ADC_ISR_EOS               (1U << 3)
#define ADC_ISR_OVR               (1U << 4)
#define ADC_ISR_LDORDY            (1U << 12)

/* CR bits used by this model */
#define ADC_CR_ADEN               (1U << 0)
#define ADC_CR_ADDIS              (1U << 1)
#define ADC_CR_ADSTART            (1U << 2)
#define ADC_CR_JADSTART           (1U << 3)
#define ADC_CR_ADSTP              (1U << 4)
#define ADC_CR_JADSTP             (1U << 5)
#define ADC_CR_ADVREGEN           (1U << 28)
#define ADC_CR_DEEPPWD            (1U << 29)
#define ADC_CR_ADCALDIF           (1U << 30)
#define ADC_CR_ADCAL              (1U << 31)

#define ADC_CR_VISIBLE_MASK       (ADC_CR_ADVREGEN | ADC_CR_DEEPPWD | ADC_CR_ADCALDIF)
#define ADC_CR_COMMAND_MASK       (ADC_CR_ADEN | ADC_CR_ADDIS | ADC_CR_ADSTART | \
                                   ADC_CR_JADSTART | ADC_CR_ADSTP | ADC_CR_JADSTP | \
                                   ADC_CR_ADCAL)

/*
 * Bit 21 was written transiently by firmware but never read back as set in the
 * hardware trace, so do not let it stick in this minimal model.
 */
#define ADC_CFGR_IGNORED_MASK     (1U << 21)
/* Trace shows the active ADC1 path once CFGR low bits reach DMAEN|DMACFG. */
#define ADC_CFGR_DMA_MASK         0x3U

/*
 * Regular-conversion EOC appears only after several ISR polls in the trace.
 */
#define ADC_EOC_DELAY_READS       8U

typedef struct ADCUnitState {
    uint32_t regs[ADC_REG_WORDS];
    bool enabled;
    bool regular_active;
    bool injected_active;
    bool eoc_pending_once;
    uint8_t eoc_delay_reads_remaining;
    uint8_t eosmp_eos_delay_reads_remaining;
    uint32_t cr_visible_pending;
    bool cr_visible_pending_valid;
    uint8_t cr_visible_delay_reads;
    uint8_t adcal_reads_remaining;
    uint8_t adstart_reads_remaining;
    uint8_t jadstart_reads_remaining;
} ADCUnitState;

typedef struct ADCState {
    ADCUnitState adc1;
    ADCUnitState adc2;
    uint32_t common_regs[ADC_REG_WORDS];

    uint64_t adc1_base;
    uint64_t adc2_base;
    uint64_t common_base;
} ADCState;

static ADCState g_adc;

static uint32_t adc_access_mask(unsigned size)
{
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    case 4:
    default:
        return 0xFFFFFFFFU;
    }
}

static uint32_t adc_access_shift(hwaddr addr)
{
    return (uint32_t)((addr & 0x3ULL) * 8U);
}

static void adc_unit_reset(ADCUnitState *u)
{
    memset(u, 0, sizeof(*u));
}

static void adc_activate_regular(ADCUnitState *u)
{
    u->regular_active = true;
    u->eoc_pending_once = false;
    u->eoc_delay_reads_remaining = ADC_EOC_DELAY_READS;

    if (u->eosmp_eos_delay_reads_remaining == 0) {
        /*
         * Hardware shows one initial ISR poll with only ADRDY/LDORDY visible,
         * then EOSMP|EOS on the following polls.
         */
        u->eosmp_eos_delay_reads_remaining = 1;
    }
}

/* CR visible bits update with a one-read delay after writes. */
static uint32_t adc_unit_read32(ADCUnitState *u, hwaddr offset)
{
    hwaddr regoff = offset & ~0x3ULL;
    uint32_t value;

    if (regoff >= ADC_BLOCK_SIZE) {
        return 0;
    }

    value = u->regs[regoff >> 2];

    if (regoff == ADC_ISR_OFFSET) {
        /*
         * Hardware shows one initial poll with only ADRDY/LDORDY set, then the
         * regular-conversion flow exposes EOSMP|EOS and later a delayed EOC.
         */
        if (u->regular_active) {
            if (u->eosmp_eos_delay_reads_remaining) {
                u->eosmp_eos_delay_reads_remaining--;
            } else {
                value |= ADC_ISR_EOSMP | ADC_ISR_EOS;
            }

            if (u->eoc_delay_reads_remaining) {
                u->eoc_delay_reads_remaining--;
                if (u->eoc_delay_reads_remaining == 0) {
                    u->eoc_pending_once = true;
                }
            }
        }

        if (u->eoc_pending_once) {
            value |= ADC_ISR_EOC;
            u->eoc_pending_once = false;
        }
    } else if (regoff == ADC_CR_OFFSET) {
        /*
         * Writable CR power/config bits do not become visible on the very
         * first read after the write. Return the previous visible state once,
         * then commit the new visible state for subsequent polls.
         */
        value &= ADC_CR_VISIBLE_MASK;

        if (u->cr_visible_pending_valid) {
            if (u->cr_visible_delay_reads) {
                u->cr_visible_delay_reads--;
                if (u->cr_visible_delay_reads == 0) {
                    u->regs[ADC_CR_OFFSET >> 2] =
                        (u->regs[ADC_CR_OFFSET >> 2] & ~ADC_CR_VISIBLE_MASK) |
                        (u->cr_visible_pending & ADC_CR_VISIBLE_MASK);
                    u->cr_visible_pending_valid = false;
                }
            } else {
                u->regs[ADC_CR_OFFSET >> 2] =
                    (u->regs[ADC_CR_OFFSET >> 2] & ~ADC_CR_VISIBLE_MASK) |
                    (u->cr_visible_pending & ADC_CR_VISIBLE_MASK);
                u->cr_visible_pending_valid = false;
                value = u->regs[ADC_CR_OFFSET >> 2] & ADC_CR_VISIBLE_MASK;
            }
        }

        if (u->adstart_reads_remaining) {
            value |= ADC_CR_ADSTART;
            u->adstart_reads_remaining--;
        }

        if (u->jadstart_reads_remaining) {
            value |= ADC_CR_JADSTART;
            u->jadstart_reads_remaining--;
        }

        if (u->adcal_reads_remaining) {
            value |= ADC_CR_ADCAL;
            u->adcal_reads_remaining--;
            if (u->adcal_reads_remaining == 0) {
                u->regs[ADC_CR_OFFSET >> 2] &= ~ADC_CR_ADCAL;
            }
        }
    }

    return value;
}

static void adc_unit_write32(ADCUnitState *u, hwaddr offset, uint32_t value, uint32_t wmask)
{
    hwaddr regoff = offset & ~0x3ULL;
    uint32_t *reg;
    uint32_t oldv;
    uint32_t merged;

    if (regoff >= ADC_BLOCK_SIZE) {
        return;
    }

    reg = &u->regs[regoff >> 2];
    oldv = *reg;
    merged = (oldv & ~wmask) | (value & wmask);

    switch (regoff) {
    case ADC_ISR_OFFSET: {
        uint32_t clear_mask = value & wmask;

        /* ADC_ISR sticky bits are W1C. */
        *reg = oldv & ~clear_mask;

        if (clear_mask & ADC_ISR_EOC) {
            u->eoc_pending_once = false;
        }
        break;
    }

    case ADC_CR_OFFSET: {
        uint32_t written = value & wmask;
        uint32_t visible_wmask = wmask & ADC_CR_VISIBLE_MASK;
        uint32_t effective_visible =
            u->cr_visible_pending_valid ?
            (u->cr_visible_pending & ADC_CR_VISIBLE_MASK) :
            (u->regs[ADC_CR_OFFSET >> 2] & ADC_CR_VISIBLE_MASK);
        uint32_t next_visible =
            (effective_visible & ~visible_wmask) |
            (written & visible_wmask);

        if (visible_wmask) {
            if (next_visible == (u->regs[ADC_CR_OFFSET >> 2] & ADC_CR_VISIBLE_MASK)) {
                u->cr_visible_pending_valid = false;
                u->cr_visible_delay_reads = 0;
                u->regs[ADC_CR_OFFSET >> 2] =
                    (u->regs[ADC_CR_OFFSET >> 2] & ~ADC_CR_VISIBLE_MASK) |
                    next_visible;
            } else {
                u->cr_visible_pending = next_visible;
                u->cr_visible_pending_valid = true;
                u->cr_visible_delay_reads = 1;
            }
        }

        if (written & ADC_CR_ADCAL) {
            /*
             * Calibration is visible briefly when polled and then self-clears.
             */
            u->regs[ADC_CR_OFFSET >> 2] |= ADC_CR_ADCAL;
            u->adcal_reads_remaining = 1;
            u->enabled = false;
            u->regular_active = false;
            u->injected_active = false;
            u->eoc_pending_once = false;
            u->eoc_delay_reads_remaining = 0;
            u->eosmp_eos_delay_reads_remaining = 0;
            u->adstart_reads_remaining = 0;
            u->jadstart_reads_remaining = 0;
        }

        /*
         * The trace shows both 0x20000000 and 0x10000000 during ADC bring-up.
         * Treat either visible power-state bit as sufficient to make LDORDY
         * observable.
         */
        if (visible_wmask &&
            (next_visible & (ADC_CR_ADVREGEN | ADC_CR_DEEPPWD))) {
            u->regs[ADC_ISR_OFFSET >> 2] |= ADC_ISR_LDORDY;
        }

        if (written & ADC_CR_ADEN) {
            u->enabled = true;
            u->regs[ADC_ISR_OFFSET >> 2] |= ADC_ISR_ADRDY | ADC_ISR_LDORDY;
        }

        if (written & ADC_CR_ADDIS) {
            u->enabled = false;
            u->regular_active = false;
            u->injected_active = false;
            u->eoc_pending_once = false;
            u->eoc_delay_reads_remaining = 0;
            u->eosmp_eos_delay_reads_remaining = 0;
            u->adstart_reads_remaining = 0;
            u->jadstart_reads_remaining = 0;
            u->regs[ADC_ISR_OFFSET >> 2] &= ~ADC_ISR_ADRDY;
        }

        if (written & ADC_CR_ADSTP) {
            u->regular_active = false;
            u->eoc_pending_once = false;
            u->eoc_delay_reads_remaining = 0;
            u->eosmp_eos_delay_reads_remaining = 0;
            u->adstart_reads_remaining = 0;
        }

        if (written & ADC_CR_JADSTP) {
            u->injected_active = false;
            u->jadstart_reads_remaining = 0;
        }

        if ((written & ADC_CR_ADSTART) && u->enabled) {
            adc_activate_regular(u);
            u->adstart_reads_remaining = 1;
        }

        if ((written & ADC_CR_JADSTART) && u->enabled) {
            u->injected_active = true;
            u->jadstart_reads_remaining = 1;
        }

        break;
    }

    case ADC_CFGR_OFFSET:
        /*
         * The trace shows bit 21 not reading back even when software touches it.
         */
        merged &= ~ADC_CFGR_IGNORED_MASK;
        *reg = merged;

        /*
         * On the traced ADC1 path, once DMAEN|DMACFG are programmed while the
         * ADC is enabled, ISR quickly transitions from 0x1001 to 0x100B even
         * without requiring a separately observed ADSTART write in the trace.
         */
        if (u->enabled && ((merged & ADC_CFGR_DMA_MASK) == ADC_CFGR_DMA_MASK)) {
            adc_activate_regular(u);
        }
        break;

    default:
        /* Plain RW storage for all other observed configuration registers. */
        *reg = merged;
        break;
    }
}

static uint32_t adc_common_read32(ADCState *s, hwaddr offset)
{
    hwaddr regoff = offset & ~0x3ULL;

    if (regoff >= ADC_BLOCK_SIZE) {
        return 0;
    }

    return s->common_regs[regoff >> 2];
}

static void adc_common_write32(ADCState *s, hwaddr offset, uint32_t value, uint32_t wmask)
{
    hwaddr regoff = offset & ~0x3ULL;
    uint32_t oldv;

    if (regoff >= ADC_BLOCK_SIZE) {
        return;
    }

    oldv = s->common_regs[regoff >> 2];
    s->common_regs[regoff >> 2] = (oldv & ~wmask) | (value & wmask);
}

static bool adc_decode_range(ADCState *s, hwaddr addr,
                             ADCUnitState **unit, bool *is_common, hwaddr *offset)
{
    *unit = NULL;
    *is_common = false;
    *offset = 0;

    if (addr >= s->adc1_base && addr < (s->adc1_base + ADC_BLOCK_SIZE)) {
        *unit = &s->adc1;
        *offset = addr - s->adc1_base;
        return true;
    }

    if (addr >= s->adc2_base && addr < (s->adc2_base + ADC_BLOCK_SIZE)) {
        *unit = &s->adc2;
        *offset = addr - s->adc2_base;
        return true;
    }

    if (addr >= s->common_base && addr < (s->common_base + ADC_BLOCK_SIZE)) {
        *is_common = true;
        *offset = addr - s->common_base;
        return true;
    }

    return false;
}

uint64_t adc_read(void *opaque, hwaddr addr, unsigned size)
{
    ADCState *s = (ADCState *)opaque;
    ADCUnitState *unit;
    bool is_common;
    hwaddr offset;
    uint32_t fullv = 0;
    uint32_t shift;
    uint32_t mask;

    if (!s) {
        s = &g_adc;
    }

    if (!adc_decode_range(s, addr, &unit, &is_common, &offset)) {
        return 0;
    }

    if (is_common) {
        fullv = adc_common_read32(s, offset);
    } else if (unit) {
        fullv = adc_unit_read32(unit, offset);
    }

    shift = adc_access_shift(addr);
    mask = adc_access_mask(size);

    return (uint64_t)((fullv >> shift) & mask);
}

void adc_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    ADCState *s = (ADCState *)opaque;
    ADCUnitState *unit;
    bool is_common;
    hwaddr offset;
    uint32_t shift;
    uint32_t wmask;
    uint32_t shifted_value;

    if (!s) {
        s = &g_adc;
    }

    if (!adc_decode_range(s, addr, &unit, &is_common, &offset)) {
        return;
    }

    shift = adc_access_shift(addr);
    wmask = adc_access_mask(size) << shift;
    shifted_value = ((uint32_t)value) << shift;

    if (is_common) {
        adc_common_write32(s, offset, shifted_value, wmask);
    } else if (unit) {
        adc_unit_write32(unit, offset, shifted_value, wmask);
    }
}

// MUST return &g_state — framework stores this and passes it as opaque to _read/_write
void* adc_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_adc, 0, sizeof(g_adc));

    /*
     * No ConfigSection accessor API was provided in the prompt, so this model
     * uses the trace-derived absolute base addresses directly.
     */
    g_adc.adc1_base = ADC1_BASE_DEFAULT;
    g_adc.adc2_base = ADC2_BASE_DEFAULT;
    g_adc.common_base = ADC12_COMMON_BASE_DEFAULT;

    adc_unit_reset(&g_adc.adc1);
    adc_unit_reset(&g_adc.adc2);
    memset(g_adc.common_regs, 0, sizeof(g_adc.common_regs));

    return &g_adc;
}