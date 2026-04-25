// FILE: model.c
#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define ADC1_BASE           0x40022000ULL
#define ADC2_BASE           0x40022100ULL
#define ADC12_COMMON_BASE   0x40022300ULL

#define ADC_INST_SIZE       0x100ULL
#define ADC_COMMON_SIZE     0x100ULL

/* Per-instance register offsets */
#define ADC_REG_ISR         0x00
#define ADC_REG_IER         0x04
#define ADC_REG_CR          0x08
#define ADC_REG_CFGR        0x0C
#define ADC_REG_CFGR2       0x10
#define ADC_REG_SMPR1       0x14
#define ADC_REG_SMPR2       0x18
#define ADC_REG_PCSEL       0x1C
#define ADC_REG_TR1         0x20
#define ADC_REG_TR2         0x24
#define ADC_REG_TR3         0x28
#define ADC_REG_SQR1        0x30
#define ADC_REG_SQR2        0x34
#define ADC_REG_SQR3        0x38
#define ADC_REG_SQR4        0x3C
#define ADC_REG_DR          0x40
#define ADC_REG_OFR1        0x60
#define ADC_REG_OFR2        0x64
#define ADC_REG_OFR3        0x68
#define ADC_REG_OFR4        0x6C
#define ADC_REG_AWD2CR      0xA0
#define ADC_REG_AWD3CR      0xA4
#define ADC_REG_DIFSEL      0xC0
#define ADC_REG_CALFACT     0xC4

/* ADC12 common */
#define ADC_COMMON_REG_CCR  0x08

/* ISR bits observed in traces */
#define ADC_ISR_ADRDY       0x00000001U
#define ADC_ISR_EOSMP       0x00000002U
#define ADC_ISR_EOC         0x00000004U
#define ADC_ISR_EOS         0x00000008U
#define ADC_ISR_CFGRDY      0x00001000U

/* CR bits relevant to observed behavior */
#define ADC_CR_ADEN         0x00000001U
#define ADC_CR_ADDIS        0x00000002U
#define ADC_CR_ADSTART      0x00000004U
#define ADC_CR_ADSTP        0x00000010U
#define ADC_CR_ADVREGEN     0x10000000U
#define ADC_CR_DEEPPWD      0x20000000U
#define ADC_CR_ADCAL        0x80000000U

#define ADC_CR_PERSIST_MASK (ADC_CR_ADVREGEN | ADC_CR_DEEPPWD)
#define ADC_CR_CMD_MASK     (ADC_CR_ADEN | ADC_CR_ADDIS | ADC_CR_ADSTART | ADC_CR_ADSTP | ADC_CR_ADCAL)

/*
 * Only these CFGR bits are seen in readback:
 *   bit31 -> 0x80000000
 *   bit13:12 -> 0x00003000
 *   bit1:0 -> 0x00000003
 */
#define ADC_CFGR_READBACK_MASK 0x80003003U
#define ADC_CFGR_DMA_MASK      0x00000003U
#define ADC_CFGR_CONT_MASK     0x00002000U

#define ADC_CONV_PERIOD_NS     13000000ULL

typedef struct {
    uint32_t isr;
    uint32_t ier;
    uint32_t cr_persist;

    uint32_t cfgr;
    uint32_t cfgr2;
    uint32_t smpr1;
    uint32_t smpr2;
    uint32_t pcsel;
    uint32_t tr1;
    uint32_t tr2;
    uint32_t tr3;
    uint32_t sqr1;
    uint32_t sqr2;
    uint32_t sqr3;
    uint32_t sqr4;
    uint32_t dr;
    uint32_t ofr1;
    uint32_t ofr2;
    uint32_t ofr3;
    uint32_t ofr4;
    uint32_t awd2cr;
    uint32_t awd3cr;
    uint32_t difsel;
    uint32_t calfact;

    bool enabled;
    bool converting;
    bool calibrating;

    unsigned calib_reads_left;
    unsigned stop_reads_left;

    uint64_t next_conv_ns;
    uint64_t period_ns;
    uint16_t sample_seed;
} ADCInst;

typedef struct {
    ADCInst adc[2];
    uint32_t ccr;
} ADCState;

static ADCState g_adc;

static uint32_t access_mask(unsigned size)
{
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    default:
        return 0xFFFFFFFFU;
    }
}

static unsigned access_shift(hwaddr addr)
{
    return (unsigned)(addr & 0x3ULL) * 8U;
}

static uint32_t extract_access(uint32_t reg, hwaddr addr, unsigned size)
{
    return (reg >> access_shift(addr)) & access_mask(size);
}

static uint32_t merge_access(uint32_t oldv, hwaddr addr, uint64_t value, unsigned size)
{
    uint32_t shift = access_shift(addr);
    uint32_t mask = access_mask(size) << shift;
    uint32_t ins = ((uint32_t)value << shift) & mask;
    return (oldv & ~mask) | ins;
}

static uint32_t write_bits_from_access(hwaddr addr, uint64_t value, unsigned size)
{
    return (((uint32_t)value) & access_mask(size)) << access_shift(addr);
}

static int adc_decode_inst(hwaddr addr, hwaddr *offset)
{
    if (addr >= ADC1_BASE && addr < ADC1_BASE + ADC_INST_SIZE) {
        *offset = addr - ADC1_BASE;
        return 0;
    }

    if (addr >= ADC2_BASE && addr < ADC2_BASE + ADC_INST_SIZE) {
        *offset = addr - ADC2_BASE;
        return 1;
    }

    return -1;
}

static bool adc_decode_common(hwaddr addr, hwaddr *offset)
{
    if (addr >= ADC12_COMMON_BASE && addr < ADC12_COMMON_BASE + ADC_COMMON_SIZE) {
        *offset = addr - ADC12_COMMON_BASE;
        return true;
    }

    return false;
}

static bool adc_dma_enabled(const ADCInst *a)
{
    return (a->cfgr & ADC_CFGR_DMA_MASK) != 0;
}

static bool adc_continuous(const ADCInst *a)
{
    return (a->cfgr & ADC_CFGR_CONT_MASK) != 0;
}

static void adc_mark_config_ready(ADCInst *a)
{
    a->isr |= ADC_ISR_CFGRDY;
}

static void adc_stop_conversion(ADCInst *a)
{
    a->converting = false;
    a->next_conv_ns = 0;
}

static uint16_t adc_next_sample(ADCInst *a)
{
    a->sample_seed = (uint16_t)((a->sample_seed + 0x31U) & 0x0FFFU);
    return a->sample_seed;
}

static void adc_finish_calibration(ADCInst *a)
{
    a->calibrating = false;
    adc_mark_config_ready(a);
}

static void adc_complete_conversion(ADCInst *a, uint64_t when_ns, bool poll_driven)
{
    a->dr = adc_next_sample(a);
    a->isr |= ADC_ISR_EOSMP | ADC_ISR_EOS;

    /*
     * Observed steady-state status is usually 0x100B, with rare 0x100F.
     * Keep EOC transient in DMA mode.
     */
    if (adc_dma_enabled(a)) {
        if (poll_driven) {
            a->isr &= ~ADC_ISR_EOC;
        } else {
            a->isr |= ADC_ISR_EOC;
        }
    } else {
        a->isr |= ADC_ISR_EOC;
    }

    if (a->enabled) {
        a->isr |= ADC_ISR_ADRDY;
    }

    if (adc_continuous(a) && a->enabled) {
        a->converting = true;
        a->next_conv_ns = when_ns + a->period_ns;
    } else {
        adc_stop_conversion(a);
    }
}

static void adc_sync_time(ADCInst *a)
{
    int64_t now_i64;
    uint64_t now;
    int guard = 0;

    if (!a->enabled || !a->converting) {
        return;
    }

    now_i64 = qemu_plugin_get_virtual_timer();
    if (now_i64 < 0) {
        return;
    }

    now = (uint64_t)now_i64;
    while (a->converting && now >= a->next_conv_ns && guard < 4) {
        uint64_t fire = a->next_conv_ns;
        adc_complete_conversion(a, fire, false);
        guard++;
    }

    if (guard == 4 && a->converting && now >= a->next_conv_ns) {
        a->next_conv_ns = now + a->period_ns;
    }
}

static void adc_poll_progress(ADCInst *a, hwaddr reg_off)
{
    int64_t now_i64;
    uint64_t now;

    if (!a->enabled || !a->converting) {
        return;
    }

    /*
     * Tight MMIO polling loops may not advance virtual time. Complete
     * conversions synchronously on status/config polling.
     */
    if (reg_off != ADC_REG_CFGR && reg_off != ADC_REG_ISR) {
        return;
    }

    now_i64 = qemu_plugin_get_virtual_timer();
    now = (now_i64 < 0) ? 0 : (uint64_t)now_i64;
    adc_complete_conversion(a, now, true);
}

static uint32_t adc_read_cr(ADCInst *a)
{
    uint32_t ret = a->cr_persist;

    if (a->enabled && a->converting) {
        ret |= ADC_CR_ADSTART;
    }
    if (a->calibrating) {
        ret |= ADC_CR_ADCAL;
    }
    if (a->stop_reads_left > 0) {
        ret |= ADC_CR_ADSTP;
    }

    if (a->calibrating && a->calib_reads_left > 0) {
        a->calib_reads_left--;
        if (a->calib_reads_left == 0) {
            adc_finish_calibration(a);
        }
    }

    if (a->stop_reads_left > 0) {
        a->stop_reads_left--;
    }

    return ret;
}

static uint32_t adc_read_isr(ADCInst *a)
{
    uint32_t ret = a->isr;

    if (adc_dma_enabled(a) && (ret & ADC_ISR_EOC)) {
        a->isr &= ~ADC_ISR_EOC;
    }

    return ret;
}

static uint32_t adc_read_inst_reg(ADCInst *a, hwaddr reg_off)
{
    switch (reg_off) {
    case ADC_REG_ISR:
        return adc_read_isr(a);
    case ADC_REG_IER:
        return a->ier;
    case ADC_REG_CR:
        return adc_read_cr(a);
    case ADC_REG_CFGR:
        return a->cfgr;
    case ADC_REG_CFGR2:
        return a->cfgr2;
    case ADC_REG_SMPR1:
        return a->smpr1;
    case ADC_REG_SMPR2:
        return a->smpr2;
    case ADC_REG_PCSEL:
        return a->pcsel;
    case ADC_REG_TR1:
        return a->tr1;
    case ADC_REG_TR2:
        return a->tr2;
    case ADC_REG_TR3:
        return a->tr3;
    case ADC_REG_SQR1:
        return a->sqr1;
    case ADC_REG_SQR2:
        return a->sqr2;
    case ADC_REG_SQR3:
        return a->sqr3;
    case ADC_REG_SQR4:
        return a->sqr4;
    case ADC_REG_DR:
        return a->dr;
    case ADC_REG_OFR1:
        return a->ofr1;
    case ADC_REG_OFR2:
        return a->ofr2;
    case ADC_REG_OFR3:
        return a->ofr3;
    case ADC_REG_OFR4:
        return a->ofr4;
    case ADC_REG_AWD2CR:
        return a->awd2cr;
    case ADC_REG_AWD3CR:
        return a->awd3cr;
    case ADC_REG_DIFSEL:
        return a->difsel;
    case ADC_REG_CALFACT:
        return a->calfact;
    default:
        return 0;
    }
}

static void adc_handle_cr_write(ADCInst *a, hwaddr addr, uint64_t value, unsigned size)
{
    uint32_t write_bits = write_bits_from_access(addr, value, size);
    uint32_t new_persist = merge_access(a->cr_persist, addr, value, size) & ADC_CR_PERSIST_MASK;
    uint32_t cmds = write_bits & ADC_CR_CMD_MASK;
    int64_t now_i64 = qemu_plugin_get_virtual_timer();
    uint64_t now = (now_i64 < 0) ? 0 : (uint64_t)now_i64;

    if (new_persist & ADC_CR_DEEPPWD) {
        new_persist &= ~ADC_CR_ADVREGEN;
    }
    a->cr_persist = new_persist;

    if (a->cr_persist & ADC_CR_DEEPPWD) {
        a->enabled = false;
        a->calibrating = false;
        a->calib_reads_left = 0;
        a->stop_reads_left = 0;
        adc_stop_conversion(a);
        a->isr &= ~(ADC_ISR_ADRDY | ADC_ISR_EOSMP | ADC_ISR_EOC | ADC_ISR_EOS);
    }

    if (cmds & ADC_CR_ADDIS) {
        a->enabled = false;
        adc_stop_conversion(a);
        a->isr &= ~(ADC_ISR_ADRDY | ADC_ISR_EOSMP | ADC_ISR_EOC | ADC_ISR_EOS);
    }

    if (cmds & ADC_CR_ADCAL) {
        if (!(a->cr_persist & ADC_CR_DEEPPWD) &&
            (a->cr_persist & ADC_CR_ADVREGEN) &&
            !a->enabled) {
            a->calibrating = true;
            a->calib_reads_left = 1;
        }
    }

    if (cmds & ADC_CR_ADEN) {
        if (!(a->cr_persist & ADC_CR_DEEPPWD) &&
            (a->cr_persist & ADC_CR_ADVREGEN) &&
            !a->calibrating) {
            a->enabled = true;
            a->isr |= ADC_ISR_ADRDY;
        }
    }

    if (cmds & ADC_CR_ADSTP) {
        adc_stop_conversion(a);
        a->stop_reads_left = 1;
    }

    if ((cmds & ADC_CR_ADSTART) && a->enabled && !a->calibrating) {
        a->converting = true;
        a->next_conv_ns = now + a->period_ns;
        a->isr &= ~(ADC_ISR_EOSMP | ADC_ISR_EOC | ADC_ISR_EOS);
    }
}

static void adc_write_inst_reg(ADCInst *a, hwaddr addr, hwaddr reg_off, uint64_t value, unsigned size)
{
    uint32_t clr;

    switch (reg_off) {
    case ADC_REG_ISR:
        clr = write_bits_from_access(addr, value, size);
        a->isr &= ~clr;
        break;

    case ADC_REG_IER:
        a->ier = merge_access(a->ier, addr, value, size);
        break;

    case ADC_REG_CR:
        adc_handle_cr_write(a, addr, value, size);
        break;

    case ADC_REG_CFGR:
        a->cfgr = merge_access(a->cfgr, addr, value, size) & ADC_CFGR_READBACK_MASK;
        adc_mark_config_ready(a);
        break;

    case ADC_REG_CFGR2:
        a->cfgr2 = merge_access(a->cfgr2, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_SMPR1:
        a->smpr1 = merge_access(a->smpr1, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_SMPR2:
        a->smpr2 = merge_access(a->smpr2, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_PCSEL:
        a->pcsel = merge_access(a->pcsel, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_TR1:
        a->tr1 = merge_access(a->tr1, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_TR2:
        a->tr2 = merge_access(a->tr2, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_TR3:
        a->tr3 = merge_access(a->tr3, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_SQR1:
        a->sqr1 = merge_access(a->sqr1, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_SQR2:
        a->sqr2 = merge_access(a->sqr2, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_SQR3:
        a->sqr3 = merge_access(a->sqr3, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_SQR4:
        a->sqr4 = merge_access(a->sqr4, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_OFR1:
        a->ofr1 = merge_access(a->ofr1, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_OFR2:
        a->ofr2 = merge_access(a->ofr2, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_OFR3:
        a->ofr3 = merge_access(a->ofr3, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_OFR4:
        a->ofr4 = merge_access(a->ofr4, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_AWD2CR:
        a->awd2cr = merge_access(a->awd2cr, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_AWD3CR:
        a->awd3cr = merge_access(a->awd3cr, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_DIFSEL:
        a->difsel = merge_access(a->difsel, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_CALFACT:
        a->calfact = merge_access(a->calfact, addr, value, size);
        adc_mark_config_ready(a);
        break;

    case ADC_REG_DR:
    default:
        break;
    }
}

uint64_t adc_read(void *opaque, hwaddr addr, unsigned size)
{
    ADCState *s = (ADCState *)opaque;
    hwaddr offset;
    hwaddr reg_off;
    int idx;

    idx = adc_decode_inst(addr, &offset);
    if (idx >= 0) {
        ADCInst *a = &s->adc[idx];
        uint32_t regv;

        reg_off = offset & ~0x3ULL;
        adc_sync_time(a);
        adc_poll_progress(a, reg_off);
        regv = adc_read_inst_reg(a, reg_off);
        return extract_access(regv, addr, size);
    }

    if (adc_decode_common(addr, &offset)) {
        reg_off = offset & ~0x3ULL;
        switch (reg_off) {
        case ADC_COMMON_REG_CCR:
            return extract_access(s->ccr, addr, size);
        default:
            return 0;
        }
    }

    return 0;
}

void adc_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    ADCState *s = (ADCState *)opaque;
    hwaddr offset;
    hwaddr reg_off;
    int idx;

    idx = adc_decode_inst(addr, &offset);
    if (idx >= 0) {
        ADCInst *a = &s->adc[idx];
        reg_off = offset & ~0x3ULL;
        adc_write_inst_reg(a, addr, reg_off, value, size);
        return;
    }

    if (adc_decode_common(addr, &offset)) {
        reg_off = offset & ~0x3ULL;
        switch (reg_off) {
        case ADC_COMMON_REG_CCR:
            s->ccr = merge_access(s->ccr, addr, value, size);
            adc_mark_config_ready(&s->adc[0]);
            adc_mark_config_ready(&s->adc[1]);
            break;
        default:
            break;
        }
    }
}

static void adc_inst_init(ADCInst *a, uint16_t seed)
{
    memset(a, 0, sizeof(*a));
    a->period_ns = ADC_CONV_PERIOD_NS;
    a->sample_seed = seed;
}

void *adc_init(ConfigSection *model_info)
{
    (void)model_info;

    memset(&g_adc, 0, sizeof(g_adc));
    adc_inst_init(&g_adc.adc[0], 0x0800);
    adc_inst_init(&g_adc.adc[1], 0x0900);

    return &g_adc;
}