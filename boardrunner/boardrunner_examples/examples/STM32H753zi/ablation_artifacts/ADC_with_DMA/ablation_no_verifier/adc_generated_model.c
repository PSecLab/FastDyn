// Device Model for ADC
#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define ADC1_BASE           0x40022000ULL
#define ADC2_BASE           0x40022100ULL
#define ADC12_COMMON_BASE   0x40022300ULL

#define ADC_INST_SIZE       0x100ULL
#define ADC_COMMON_SIZE     0x100ULL

/* Regular ADC register offsets */
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
#define ADC_REG_DIFSEL      0xB0
#define ADC_REG_CALFACT     0xB4

/* ADC12 common */
#define ADC_COMMON_REG_CCR  0x08

/* Minimal bit semantics inferred from trace + RM behavior */
#define ADC_ISR_ADRDY       0x00000001U
#define ADC_ISR_EOSMP       0x00000002U
#define ADC_ISR_EOS         0x00000008U
#define ADC_ISR_CFGRDY      0x00001000U   /* observed steady-state bit in 0x100B */

#define ADC_CR_ADEN         0x00000001U
#define ADC_CR_ADDIS        0x00000002U
#define ADC_CR_ADSTART      0x00000004U
#define ADC_CR_ADSTP        0x00000010U
#define ADC_CR_ADCAL        0x80000000U

/* Trace-backed heuristic: low CFGR bits enable DMA management */
#define ADC_CFGR_DMA_MASK   0x00000003U
/* Common STM32 ADC continuous mode placement; used only as a heuristic */
#define ADC_CFGR_CONT       0x00002000U

#define ADC_TIMER_PERIOD_NS 1000000ULL      /* 1 ms periodic poll */
#define ADC_CONV_PERIOD_NS  13000000ULL     /* approx observed DMA IRQ cadence */

#define ADC1_FIFO_PATH      "/tmp/stm32h7_adc1_in"
#define ADC2_FIFO_PATH      "/tmp/stm32h7_adc2_in"

typedef struct {
    uint32_t isr;
    uint32_t ier;
    uint32_t cr;      /* stores only persistent software bits; command bits self-clear */
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

    uint16_t ramp;
    uint64_t period_ns;
    uint64_t next_conv_ns;

    int dma_stream_id;

    int fifo_fd;
    uint8_t fifo_accum[2];
    int fifo_fill;

    const char *name;
    const char *fifo_path;
} ADCInst;

typedef struct {
    ADCInst adc[2];
    uint32_t ccr;
    uint64_t timer;
} ADCState;

static ADCState g_adc;

static void adc_debugf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    dev_debug(buf);
}

static uint32_t access_mask(unsigned size)
{
    switch (size) {
    case 1: return 0xFFU;
    case 2: return 0xFFFFU;
    default: return 0xFFFFFFFFU;
    }
}

static unsigned access_shift(hwaddr addr)
{
    return (unsigned)(addr & 0x3ULL) * 8U;
}

static uint32_t extract_access(uint32_t reg, hwaddr addr, unsigned size)
{
    unsigned shift = access_shift(addr);
    uint32_t mask = access_mask(size);
    return (reg >> shift) & mask;
}

static uint32_t merge_access(uint32_t oldv, hwaddr addr, uint64_t value, unsigned size)
{
    unsigned shift = access_shift(addr);
    uint32_t mask = access_mask(size) << shift;
    uint32_t ins = ((uint32_t)value << shift) & mask;
    return (oldv & ~mask) | ins;
}

static int adc_decode_inst(hwaddr addr, hwaddr *offset)
{
    if (addr >= ADC1_BASE && addr < (ADC1_BASE + ADC_INST_SIZE)) {
        *offset = addr - ADC1_BASE;
        return 0;
    }
    if (addr >= ADC2_BASE && addr < (ADC2_BASE + ADC_INST_SIZE)) {
        *offset = addr - ADC2_BASE;
        return 1;
    }
    return -1;
}

static bool adc_is_common(hwaddr addr, hwaddr *offset)
{
    if (addr >= ADC12_COMMON_BASE && addr < (ADC12_COMMON_BASE + ADC_COMMON_SIZE)) {
        *offset = addr - ADC12_COMMON_BASE;
        return true;
    }
    return false;
}

static void adc_mark_config_ready(ADCInst *a)
{
    a->isr |= ADC_ISR_CFGRDY;
}

static uint16_t adc_next_sample(ADCInst *a)
{
    uint8_t b;

    if (a->fifo_fd >= 0) {
        while (a->fifo_fill < 2) {
            if (api_fifo_read_nonblock(a->fifo_fd, &b) != 1) {
                break;
            }
            a->fifo_accum[a->fifo_fill++] = b;
        }

        if (a->fifo_fill >= 2) {
            uint16_t v = (uint16_t)a->fifo_accum[0] |
                         ((uint16_t)a->fifo_accum[1] << 8);
            a->fifo_fill = 0;
            return v;
        }
    }

    /* Fallback synthetic ramp if host provides no sample stream */
    a->ramp = (uint16_t)((a->ramp + 0x31U) & 0x0FFFU);
    return a->ramp;
}

static void adc_issue_dma_if_needed(ADCInst *a)
{
    if (a->dma_stream_id < 0) {
        return;
    }

    if ((a->cfgr & ADC_CFGR_DMA_MASK) == 0) {
        return;
    }

    {
        uint8_t payload[2];
        payload[0] = (uint8_t)(a->dr & 0xFFU);
        payload[1] = (uint8_t)((a->dr >> 8) & 0xFFU);
        (void)api_dma_request_data(a->dma_stream_id, payload, 2);
    }
}

static void adc_complete_conversion(ADCInst *a, uint64_t when_ns)
{
    uint16_t sample = adc_next_sample(a);

    a->dr = sample;
    a->isr |= ADC_ISR_EOSMP | ADC_ISR_EOS;
    if (a->enabled) {
        a->isr |= ADC_ISR_ADRDY;
    }

    adc_issue_dma_if_needed(a);

    if ((a->cfgr & ADC_CFGR_CONT) == 0) {
        a->converting = false;
        a->next_conv_ns = 0;
    } else {
        a->next_conv_ns = when_ns + a->period_ns;
    }
}

static void adc_sync_inst(ADCInst *a)
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

    if (a->next_conv_ns == 0) {
        a->next_conv_ns = now + a->period_ns;
        return;
    }

    while (now >= a->next_conv_ns && guard < 8) {
        uint64_t fire = a->next_conv_ns;
        adc_complete_conversion(a, fire);
        if (!a->converting) {
            break;
        }
        guard++;
    }

    if (guard == 8 && a->converting && now >= a->next_conv_ns) {
        a->next_conv_ns = now + a->period_ns;
    }
}

static void adc_timer_cb(void *opaque)
{
    ADCState *s = (ADCState *)opaque;
    adc_sync_inst(&s->adc[0]);
    adc_sync_inst(&s->adc[1]);
}

static uint32_t adc_read_inst_reg(ADCInst *a, hwaddr reg_off)
{
    switch (reg_off) {
    case ADC_REG_ISR:     return a->isr;
    case ADC_REG_IER:     return a->ier;
    case ADC_REG_CR:      return a->cr;
    case ADC_REG_CFGR:    return a->cfgr;
    case ADC_REG_CFGR2:   return a->cfgr2;
    case ADC_REG_SMPR1:   return a->smpr1;
    case ADC_REG_SMPR2:   return a->smpr2;
    case ADC_REG_PCSEL:   return a->pcsel;
    case ADC_REG_TR1:     return a->tr1;
    case ADC_REG_TR2:     return a->tr2;
    case ADC_REG_TR3:     return a->tr3;
    case ADC_REG_SQR1:    return a->sqr1;
    case ADC_REG_SQR2:    return a->sqr2;
    case ADC_REG_SQR3:    return a->sqr3;
    case ADC_REG_SQR4:    return a->sqr4;
    case ADC_REG_DR:      return a->dr;
    case ADC_REG_OFR1:    return a->ofr1;
    case ADC_REG_OFR2:    return a->ofr2;
    case ADC_REG_OFR3:    return a->ofr3;
    case ADC_REG_OFR4:    return a->ofr4;
    case ADC_REG_AWD2CR:  return a->awd2cr;
    case ADC_REG_AWD3CR:  return a->awd3cr;
    case ADC_REG_DIFSEL:  return a->difsel;
    case ADC_REG_CALFACT: return a->calfact;
    default:              return 0;
    }
}

static void adc_write_inst_reg(ADCInst *a, hwaddr addr, hwaddr reg_off, uint64_t value, unsigned size)
{
    uint32_t merged;
    int64_t now_i64 = qemu_plugin_get_virtual_timer();
    uint64_t now = (now_i64 < 0) ? 0 : (uint64_t)now_i64;

    switch (reg_off) {
    case ADC_REG_ISR: {
        unsigned shift = access_shift(addr);
        uint32_t clr = ((uint32_t)value & access_mask(size)) << shift;
        a->isr &= ~clr;
        break;
    }

    case ADC_REG_IER:
        a->ier = merge_access(a->ier, addr, value, size);
        break;

    case ADC_REG_CR: {
        uint32_t cmd;
        const uint32_t one_shot = ADC_CR_ADEN | ADC_CR_ADDIS |
                                  ADC_CR_ADSTART | ADC_CR_ADSTP |
                                  ADC_CR_ADCAL;

        merged = merge_access(a->cr, addr, value, size);
        cmd = merged & one_shot;
        a->cr = merged & ~one_shot;

        if (cmd & ADC_CR_ADDIS) {
            a->enabled = false;
            a->converting = false;
            a->next_conv_ns = 0;
            a->isr &= ~ADC_ISR_ADRDY;
        }

        if (cmd & ADC_CR_ADEN) {
            a->enabled = true;
            a->isr |= ADC_ISR_ADRDY;
        }

        if (cmd & ADC_CR_ADCAL) {
            adc_mark_config_ready(a);
        }

        if ((cmd & ADC_CR_ADSTART) && a->enabled) {
            a->converting = true;
            if (a->next_conv_ns == 0 || a->next_conv_ns < now) {
                a->next_conv_ns = now + a->period_ns;
            }
        }

        if (cmd & ADC_CR_ADSTP) {
            a->converting = false;
            a->next_conv_ns = 0;
        }
        break;
    }

    case ADC_REG_CFGR:
        a->cfgr = merge_access(a->cfgr, addr, value, size);
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
        /* Read-only in practice; ignore writes */
        break;

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

        adc_sync_inst(a);
        reg_off = offset & ~0x3ULL;
        regv = adc_read_inst_reg(a, reg_off);
        return extract_access(regv, addr, size);
    }

    if (adc_is_common(addr, &offset)) {
        uint32_t regv = 0;
        reg_off = offset & ~0x3ULL;

        switch (reg_off) {
        case ADC_COMMON_REG_CCR:
            regv = s->ccr;
            break;
        default:
            regv = 0;
            break;
        }

        return extract_access(regv, addr, size);
    }

    adc_debugf("ADC: read from unknown addr 0x%llx", (unsigned long long)addr);
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

        adc_sync_inst(a);
        reg_off = offset & ~0x3ULL;
        adc_write_inst_reg(a, addr, reg_off, value, size);
        return;
    }

    if (adc_is_common(addr, &offset)) {
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
        return;
    }

    adc_debugf("ADC: write to unknown addr 0x%llx = 0x%llx",
               (unsigned long long)addr,
               (unsigned long long)value);
}

/* MUST return &g_state — framework stores this and passes it as opaque to _read/_write */
void* adc_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_adc, 0, sizeof(g_adc));

    g_adc.adc[0].name = "ADC1";
    g_adc.adc[0].fifo_path = ADC1_FIFO_PATH;
    g_adc.adc[0].fifo_fd = api_fifo_open(ADC1_FIFO_PATH);
    g_adc.adc[0].period_ns = ADC_CONV_PERIOD_NS;
    g_adc.adc[0].dma_stream_id = 1; /* inferred from DMA1 Stream1 + DMAMUX1 C1CR=0x9 */
    g_adc.adc[0].ramp = 0x0800;

    g_adc.adc[1].name = "ADC2";
    g_adc.adc[1].fifo_path = ADC2_FIFO_PATH;
    g_adc.adc[1].fifo_fd = api_fifo_open(ADC2_FIFO_PATH);
    g_adc.adc[1].period_ns = ADC_CONV_PERIOD_NS;
    g_adc.adc[1].dma_stream_id = -1; /* no trace-backed DMA stream mapping inferred */
    g_adc.adc[1].ramp = 0x0900;

    if (g_adc.adc[0].fifo_fd < 0) {
        adc_debugf("ADC: failed to open %s", ADC1_FIFO_PATH);
    }
    if (g_adc.adc[1].fifo_fd < 0) {
        adc_debugf("ADC: failed to open %s", ADC2_FIFO_PATH);
    }

    g_adc.timer = qemu_plugin_timer_new_period_ns(adc_timer_cb, &g_adc, ADC_TIMER_PERIOD_NS);

    return &g_adc;
}
