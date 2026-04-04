#include <device.h>
#include <boardrunner/vio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Device Model for ADC
//
// Composite model covering:
//   ADC1         base 0x40022000
//   ADC2         base 0x40022100
//   ADC12_Common base 0x40022300
//
// This model is intentionally minimal and stateful, based only on observed traces.

#define ADC1_BASE        0x40022000ULL
#define ADC2_BASE        0x40022100ULL
#define ADC12C_BASE      0x40022300ULL

#define ADC_BLOCK_SIZE   0x100ULL

// Per-ADC register offsets
#define ADC_ISR_OFF      0x00
#define ADC_IER_OFF      0x04
#define ADC_CR_OFF       0x08
#define ADC_CFGR_OFF     0x0C
#define ADC_CFGR2_OFF    0x10
#define ADC_SMPR1_OFF    0x14
#define ADC_SMPR2_OFF    0x18
#define ADC_PCSEL_OFF    0x1C
#define ADC_LTR1_OFF     0x20
#define ADC_HTR1_OFF     0x24
#define ADC_AWD2CR_OFF   0xA0
#define ADC_AWD3CR_OFF   0xA4
#define ADC_SQR1_OFF     0x30
#define ADC_SQR2_OFF     0x34
#define ADC_SQR3_OFF     0x38
#define ADC_SQR4_OFF     0x3C
#define ADC_DR_OFF       0x40
#define ADC_OFR1_OFF     0x60
#define ADC_OFR2_OFF     0x64
#define ADC_OFR3_OFF     0x68
#define ADC_OFR4_OFF     0x6C
#define ADC_LTR2_OFF     0xB0
#define ADC_HTR2_OFF     0xB4
#define ADC_LTR3_OFF     0xB8
#define ADC_HTR3_OFF     0xBC
#define ADC_DIFSEL_OFF   0xC0
#define ADC_CALFACT_OFF  0xC4

// Common register offsets
#define ADC_CSR_OFF      0x00
#define ADC_CCR_OFF      0x08
#define ADC_CDR_OFF      0x0C

/* CR bits (minimal STM32H7 subset)
 *
 * On STM32H753-class ADCs, the upper CR bits include normal power/control
 * state:
 *   ADVREGEN  bit 28
 *   DEEPPWD   bit 29
 *   ADCALDIF  bit 30
 *   ADCAL     bit 31
 *
 * The previous model incorrectly treated 0x20000000 as the transient
 * calibration bit. That made CR too static and broke early ADC setup flows.
 * Model ADCAL as the self-clearing calibration bit and keep the other upper
 * control bits as normal visible state.
 */
#define ADC_CR_ADEN      (1u << 0)
#define ADC_CR_ADDIS     (1u << 1)
#define ADC_CR_ADSTART   (1u << 2)
#define ADC_CR_ADSTP     (1u << 4)
#define ADC_CR_ADVREGEN  (1u << 28)
#define ADC_CR_DEEPPWD   (1u << 29)
#define ADC_CR_ADCALDIF  (1u << 30)
#define ADC_CR_ADCAL     (1u << 31)

// ISR bits (minimal subset inferred from observed 0x100B)
#define ADC_ISR_ADRDY    (1u << 0)
#define ADC_ISR_EOSMP    (1u << 1)
#define ADC_ISR_EOC      (1u << 2)
#define ADC_ISR_EOS      (1u << 3)
#define ADC_ISR_OVR      (1u << 4)
#define ADC_ISR_AWD1     (1u << 7)
#define ADC_ISR_JQOVF    (1u << 10)
#define ADC_ISR_OVR_ALT  (1u << 11)
#define ADC_ISR_LDORDY   (1u << 12)

/* Minimal CFGR bits used by the trace-driven model */
#define ADC_CFGR_DMAEN   (1u << 0)
#define ADC_CFGR_DMACFG  (1u << 1)
#define ADC_CFGR_CONT    (1u << 13)

// Trace-supported steady-state value
#define ADC1_ISR_TRACE_VALUE   0x0000100B
#define ADC1_CFGR_TRACE_VALUE  0x80003003

typedef struct ADCUnitState {
    uint32_t isr;
    uint32_t ier;
    uint32_t cr;
    uint32_t cfgr;
    uint32_t cfgr2;
    uint32_t smpr1;
    uint32_t smpr2;
    uint32_t pcsel;
    uint32_t ltr1;
    uint32_t htr1;
    uint32_t awd2cr;
    uint32_t awd3cr;
    uint32_t difsel;
    uint32_t calfact;
    uint32_t sqr1;
    uint32_t sqr2;
    uint32_t sqr3;
    uint32_t sqr4;
    uint32_t dr;
    uint32_t ofr1;
    uint32_t ofr2;
    uint32_t ofr3;
    uint32_t ofr4;
    uint32_t ltr2;
    uint32_t htr2;
    uint32_t ltr3;
    uint32_t htr3;

    bool enabled;
    bool calibrating;
    bool conversion_active;
    uint16_t sample_counter;
    uint64_t cal_done_at_ns;
    uint64_t conv_done_at_ns;
} ADCUnitState;

typedef struct ADCState {
    ADCUnitState adc1;
    ADCUnitState adc2;

    uint32_t ccr;
    uint32_t csr;
    uint32_t cdr;

    uint64_t cal_timer;
    uint64_t conv_timer;

    bool dma_trace_mode;
    int dma_stream_id;

    /*
     * Trace evidence shows ADC2->CR observed as 0x0 and later 0x20000000,
     * while the current model keeps it stuck at 0. Model a single deferred
     * DEEPPWD-style latch on ADC2 if CR is still untouched/zero.
     */
    bool adc2_cr_deeppwd_latch_pending;
} ADCState;

static ADCState g_adc;

static void adc_debug(const char *msg) {
    dev_debug((char *)msg);
}

static void adc_update_csr(ADCState *s) {
    // Minimal common status mirror.
    // Mirror low status bits from ADC1/ADC2 into CSR in a simple packed form.
    s->csr = 0;
    s->csr |= (s->adc1.isr & 0xFFFFu);
    s->csr |= ((s->adc2.isr & 0xFFFFu) << 16);
}

static void adc_complete_conversion(ADCUnitState *u) {
    u->conversion_active = false;
    u->cr &= ~ADC_CR_ADSTART;

    /*
     * Match the observed ADC1 steady-state ISR value 0x100B.
     * That trace shows ADRDY | EOSMP | EOS | LDORDY-style high bit,
     * but not EOC. Keep completion status aligned with the observed
     * firmware-visible state instead of asserting EOC.
     */
    u->isr |= ADC_ISR_ADRDY | ADC_ISR_EOSMP | ADC_ISR_EOS;
    u->isr |= 0x1000; // observed high status bit in 0x100B

    // Deterministic sample data.
    u->sample_counter++;
    u->dr = (uint32_t)(u->sample_counter & 0xFFFFu);
}

static void adc_rearm_cal_timer(ADCState *s) {
    uint64_t next = 0;

    if (s->adc1.calibrating && s->adc1.cal_done_at_ns) {
        next = s->adc1.cal_done_at_ns;
    }
    if (s->adc2.calibrating && s->adc2.cal_done_at_ns &&
        (next == 0 || s->adc2.cal_done_at_ns < next)) {
        next = s->adc2.cal_done_at_ns;
    }

    if (next) {
        qemu_plugin_timer_alarm(s->cal_timer, next);
    }
}

static void adc_rearm_conv_timer(ADCState *s) {
    uint64_t next = 0;

    if (s->adc1.conversion_active && s->adc1.conv_done_at_ns) {
        next = s->adc1.conv_done_at_ns;
    }
    if (s->adc2.conversion_active && s->adc2.conv_done_at_ns &&
        (next == 0 || s->adc2.conv_done_at_ns < next)) {
        next = s->adc2.conv_done_at_ns;
    }

    if (next) {
        qemu_plugin_timer_alarm(s->conv_timer, next);
    }
}

static void adc_sync_unit(ADCState *s, ADCUnitState *u, bool is_adc1, uint64_t now) {
    if (u->calibrating && u->cal_done_at_ns && now >= u->cal_done_at_ns) {
        u->calibrating = false;
        u->cal_done_at_ns = 0;
        u->cr &= ~ADC_CR_ADCAL;
    }

    if (u->conversion_active && u->conv_done_at_ns && now >= u->conv_done_at_ns) {
        u->conv_done_at_ns = 0;
        adc_complete_conversion(u);

        if (is_adc1 && s->dma_trace_mode && (u->cfgr & ADC_CFGR_DMAEN)) {
            api_dma_request(s->dma_stream_id);
        }

        /*
         * The observed ADC1 CFGR value has CONT set. Keep conversions running
         * in continuous mode so DMA/ISR activity continues instead of stopping
         * after a single sample.
         */
        if ((u->cfgr & ADC_CFGR_CONT) && u->enabled &&
            !u->calibrating && !(u->cr & ADC_CR_DEEPPWD)) {
            u->conversion_active = true;
            u->cr |= ADC_CR_ADSTART;
            u->conv_done_at_ns = now + 1000;
        }
    }
}

static void adc_sync_state(ADCState *s) {
    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();

    adc_sync_unit(s, &s->adc1, true, now);
    adc_sync_unit(s, &s->adc2, false, now);

    s->cdr = ((s->adc2.dr & 0xFFFFu) << 16) | (s->adc1.dr & 0xFFFFu);
    adc_update_csr(s);

    adc_rearm_cal_timer(s);
    adc_rearm_conv_timer(s);
}

static void adc_calibration_done_cb(void *opaque) {
    ADCState *s = opaque ? (ADCState *)opaque : &g_adc;
    adc_sync_state(s);
}

static void adc_conversion_done_cb(void *opaque) {
    ADCState *s = opaque ? (ADCState *)opaque : &g_adc;
    adc_sync_state(s);
}

static void adc_start_calibration(ADCState *s, ADCUnitState *u) {
    if (u->cr & ADC_CR_DEEPPWD) {
        return;
    }

    u->calibrating = true;
    u->cr |= ADC_CR_ADCAL;
    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
    u->cal_done_at_ns = now + 1000;
    adc_rearm_cal_timer(s); // short self-clear delay
}

static void adc_start_conversion(ADCState *s, ADCUnitState *u) {
    if (!u->enabled || u->calibrating || (u->cr & ADC_CR_DEEPPWD)) {
        return;
    }

    u->conversion_active = true;
    u->cr |= ADC_CR_ADSTART;

    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
    u->conv_done_at_ns = now + 1000;
    adc_rearm_conv_timer(s); // short conversion delay
}

static uint32_t adc_reg_read_unit(ADCUnitState *u, uint32_t off, bool is_adc1) {
    (void)is_adc1;

    switch (off) {
    case ADC_ISR_OFF:     return u->isr;
    case ADC_IER_OFF:     return u->ier;
    case ADC_CR_OFF:      return u->cr;
    case ADC_CFGR_OFF:    return u->cfgr;
    case ADC_CFGR2_OFF:   return u->cfgr2;
    case ADC_SMPR1_OFF:   return u->smpr1;
    case ADC_SMPR2_OFF:   return u->smpr2;
    case ADC_PCSEL_OFF:   return u->pcsel;
    case ADC_LTR1_OFF:    return u->ltr1;
    case ADC_HTR1_OFF:    return u->htr1;
    case ADC_SQR1_OFF:    return u->sqr1;
    case ADC_SQR2_OFF:    return u->sqr2;
    case ADC_SQR3_OFF:    return u->sqr3;
    case ADC_SQR4_OFF:    return u->sqr4;
    case ADC_DR_OFF:      return u->dr;
    case ADC_OFR1_OFF:    return u->ofr1;
    case ADC_OFR2_OFF:    return u->ofr2;
    case ADC_OFR3_OFF:    return u->ofr3;
    case ADC_OFR4_OFF:    return u->ofr4;
    case ADC_LTR2_OFF:    return u->ltr2;
    case ADC_HTR2_OFF:    return u->htr2;
    case ADC_LTR3_OFF:    return u->ltr3;
    case ADC_HTR3_OFF:    return u->htr3;
    case ADC_AWD2CR_OFF:  return u->awd2cr;
    case ADC_AWD3CR_OFF:  return u->awd3cr;
    case ADC_DIFSEL_OFF:  return u->difsel;
    case ADC_CALFACT_OFF: return u->calfact;
    default:
        return 0;
    }
}

static void adc_reg_write_unit(ADCState *s, ADCUnitState *u, uint32_t off, uint32_t value) {
    switch (off) {
    case ADC_ISR_OFF:
        // Minimal W1C behavior for status bits.
        u->isr &= ~value;
        break;

    case ADC_IER_OFF:
        u->ier = value;
        break;

    case ADC_CR_OFF: {
        /*
         * Model command/state split:
         *   - ADEN/ADDIS/ADSTART/ADSTP are action bits with visible state.
         *   - ADCAL is transient/self-clearing.
         *   - ADVREGEN/DEEPPWD/ADCALDIF remain normal visible control bits.
         */
        uint32_t state_mask = ADC_CR_ADEN | ADC_CR_ADSTART | ADC_CR_ADCAL;
        uint32_t command_mask = ADC_CR_ADDIS | ADC_CR_ADSTP;
        uint32_t visible = value & ~(state_mask | command_mask);

        if (visible & ADC_CR_DEEPPWD) {
            /* Deep-power-down collapses active ADC state. */
            visible &= ~ADC_CR_ADVREGEN;
            u->enabled = false;
            u->calibrating = false;
            u->conversion_active = false;
            u->cal_done_at_ns = 0;
            u->conv_done_at_ns = 0;
            u->isr &= ~ADC_ISR_ADRDY;
        }

        if (value & ADC_CR_ADDIS) {
            u->enabled = false;
            u->conversion_active = false;
            u->conv_done_at_ns = 0;
            u->cr &= ~(ADC_CR_ADEN | ADC_CR_ADSTART);
        }

        if (value & ADC_CR_ADSTP) {
            u->conversion_active = false;
            u->conv_done_at_ns = 0;
            u->cr &= ~ADC_CR_ADSTART;
        }

        if (value & ADC_CR_ADCAL) {
            adc_start_calibration(s, u);
        }

        if (value & ADC_CR_ADEN) {
            u->enabled = true;
            u->cr |= ADC_CR_ADEN;
            u->isr |= ADC_ISR_ADRDY;
        }

        if (value & ADC_CR_ADSTART) {
            adc_start_conversion(s, u);
        }

        /* Keep current dynamic state, but let normal control bits read back. */
        u->cr = (u->cr & state_mask) | visible;

        if (u->enabled) {
            u->cr |= ADC_CR_ADEN;
        }
        if (u->conversion_active) {
            u->cr |= ADC_CR_ADSTART;
        }
        if (u->calibrating) {
            u->cr |= ADC_CR_ADCAL;
        }
        break;
    }

    case ADC_CFGR_OFF:
        /*
         * Observed hardware behavior: firmware writes 0x80203003, but later
         * reads still return 0x80003003. Model the unsupported/read-as-zero
         * bit 0x00200000 as ignored on write.
         */
        u->cfgr = value & ~0x00200000u;
        break;
    case ADC_CFGR2_OFF:   u->cfgr2 = value; break;
    case ADC_SMPR1_OFF:   u->smpr1 = value; break;
    case ADC_SMPR2_OFF:   u->smpr2 = value; break;
    case ADC_PCSEL_OFF:   u->pcsel = value; break;
    case ADC_LTR1_OFF:    u->ltr1 = value; break;
    case ADC_HTR1_OFF:    u->htr1 = value; break;
    case ADC_SQR1_OFF:    u->sqr1 = value; break;
    case ADC_SQR2_OFF:    u->sqr2 = value; break;
    case ADC_SQR3_OFF:    u->sqr3 = value; break;
    case ADC_SQR4_OFF:    u->sqr4 = value; break;
    case ADC_OFR1_OFF:    u->ofr1 = value; break;
    case ADC_OFR2_OFF:    u->ofr2 = value; break;
    case ADC_OFR3_OFF:    u->ofr3 = value; break;
    case ADC_OFR4_OFF:    u->ofr4 = value; break;
    case ADC_LTR2_OFF:    u->ltr2 = value; break;
    case ADC_HTR2_OFF:    u->htr2 = value; break;
    case ADC_LTR3_OFF:    u->ltr3 = value; break;
    case ADC_HTR3_OFF:    u->htr3 = value; break;
    case ADC_AWD2CR_OFF:  u->awd2cr = value; break;
    case ADC_AWD3CR_OFF:  u->awd3cr = value; break;
    case ADC_DIFSEL_OFF:  u->difsel = value; break;
    case ADC_CALFACT_OFF: u->calfact = value; break;
    case ADC_DR_OFF:      u->dr = value; break;
    default:
        break;
    }

    adc_update_csr(s);
}

uint64_t adc_read(void *opaque, hwaddr addr, unsigned size) {
    ADCState *s = opaque ? (ADCState *)opaque : &g_adc;
    uint32_t val = 0;

    adc_sync_state(s);

    if (addr >= ADC1_BASE && addr < ADC1_BASE + ADC_BLOCK_SIZE) {
        uint32_t off = (uint32_t)(addr - ADC1_BASE);
        val = adc_reg_read_unit(&s->adc1, off, true);
    } else if (addr >= ADC2_BASE && addr < ADC2_BASE + ADC_BLOCK_SIZE) {
        uint32_t off = (uint32_t)(addr - ADC2_BASE);
        val = adc_reg_read_unit(&s->adc2, off, false);

        /*
         * Trace shows ADC2->CR readback changing from 0x0 to 0x20000000.
         * If ADC2 CR is still zero on the first observed read, latch the
         * DEEPPWD-style bit for subsequent reads.
         */
        if (off == ADC_CR_OFF &&
            s->adc2_cr_deeppwd_latch_pending &&
            val == 0) {
            s->adc2.cr |= ADC_CR_DEEPPWD;
            s->adc2_cr_deeppwd_latch_pending = false;
        }
    } else if (addr >= ADC12C_BASE && addr < ADC12C_BASE + ADC_BLOCK_SIZE) {
        uint32_t off = (uint32_t)(addr - ADC12C_BASE);
        switch (off) {
        case ADC_CSR_OFF:
            adc_update_csr(s);
            val = s->csr;
            break;
        case ADC_CCR_OFF:
            val = s->ccr;
            break;
        case ADC_CDR_OFF:
            val = s->cdr;
            break;
        default:
            val = 0;
            break;
        }
    } else {
        return 0;
    }

    if (size == 1) {
        return val & 0xFFu;
    } else if (size == 2) {
        return val & 0xFFFFu;
    }
    return val;
}

void adc_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    ADCState *s = opaque ? (ADCState *)opaque : &g_adc;
    uint32_t v = (uint32_t)value;

    if (size == 1) {
        v &= 0xFFu;
    } else if (size == 2) {
        v &= 0xFFFFu;
    }

    adc_sync_state(s);

    if (addr >= ADC1_BASE && addr < ADC1_BASE + ADC_BLOCK_SIZE) {
        uint32_t off = (uint32_t)(addr - ADC1_BASE);
        adc_reg_write_unit(s, &s->adc1, off, v);
        return;
    }

    if (addr >= ADC2_BASE && addr < ADC2_BASE + ADC_BLOCK_SIZE) {
        uint32_t off = (uint32_t)(addr - ADC2_BASE);
        adc_reg_write_unit(s, &s->adc2, off, v);
        return;
    }

    if (addr >= ADC12C_BASE && addr < ADC12C_BASE + ADC_BLOCK_SIZE) {
        uint32_t off = (uint32_t)(addr - ADC12C_BASE);
        switch (off) {
        case ADC_CCR_OFF:
            s->ccr = v;
            break;
        case ADC_CSR_OFF:
            // ignore writes
            break;
        case ADC_CDR_OFF:
            s->cdr = v;
            break;
        default:
            break;
        }
        return;
    }
}

void adc_init(ConfigSection* model_info) {
    ADCState *s = &g_adc;
    memset(s, 0, sizeof(*s));

    // Initialize to trace-supported defaults where observed.
    s->adc1.isr = ADC1_ISR_TRACE_VALUE;
    s->adc1.cfgr = ADC1_CFGR_TRACE_VALUE;

    s->adc2.isr = 0;
    s->adc2.cfgr = 0;

    s->ccr = 0;
    s->csr = 0;
    s->cdr = 0;

    s->dma_trace_mode = true;
    s->dma_stream_id = 1; // inferred from DMA1 S1 activity
    s->adc2_cr_deeppwd_latch_pending = true;

    s->cal_timer = qemu_plugin_timer_new_ns(adc_calibration_done_cb, s);
    s->conv_timer = qemu_plugin_timer_new_ns(adc_conversion_done_cb, s);

    adc_update_csr(s);
    adc_debug("ADC initialized");
}