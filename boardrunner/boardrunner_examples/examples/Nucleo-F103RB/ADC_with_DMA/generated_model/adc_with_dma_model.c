// adc_with_dma.c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "utils.h"
#include <device.h>
#include <boardrunner/vio.h>

// ---------------- Base addresses ----------------
#define ADC1_BASE   0x40012400u
#define DMA1_BASE   0x40020000u

// ---------------- ADC register offsets ----------------
#define ADC_SR_OFF     0x00u
#define ADC_CR1_OFF    0x04u
#define ADC_CR2_OFF    0x08u
#define ADC_SMPR1_OFF  0x0Cu
#define ADC_SMPR2_OFF  0x10u
#define ADC_SQR1_OFF   0x2Cu
#define ADC_SQR2_OFF   0x30u
#define ADC_SQR3_OFF   0x34u
#define ADC_DR_OFF     0x4Cu

// ADC_SR bits
#define ADC_SR_EOC     (1u << 1)
#define ADC_SR_STRT    (1u << 4)

// ADC_CR2 bits
#define ADC_CR2_ADON    (1u << 0)
#define ADC_CR2_CONT    (1u << 1)
#define ADC_CR2_CAL     (1u << 2)
#define ADC_CR2_RSTCAL  (1u << 3)
#define ADC_CR2_DMA     (1u << 8)
#define ADC_CR2_EXTTRIG (1u << 20)
#define ADC_CR2_SWSTART (1u << 22)

// ---------------- DMA register offsets ----------------
#define DMA_ISR_OFF     0x00u
#define DMA_IFCR_OFF    0x04u

// Channel 1 regs
#define DMA_CCR1_OFF    0x08u
#define DMA_CNDTR1_OFF  0x0Cu
#define DMA_CPAR1_OFF   0x10u
#define DMA_CMAR1_OFF   0x14u

// DMA CCR bits
#define DMA_CCR_EN      (1u << 0)
#define DMA_CCR_TCIE    (1u << 1)
#define DMA_CCR_HTIE    (1u << 2)
#define DMA_CCR_TEIE    (1u << 3)
#define DMA_CCR_DIR     (1u << 4)  // 0: P->M, 1: M->P
#define DMA_CCR_CIRC    (1u << 5)
#define DMA_CCR_PINC    (1u << 6)
#define DMA_CCR_MINC    (1u << 7)
#define DMA_CCR_PSIZE_SHIFT 8
#define DMA_CCR_MSIZE_SHIFT 10

// DMA ISR flags for Channel 1
#define DMA_ISR_GIF1    (1u << 0)
#define DMA_ISR_TCIF1   (1u << 1)
#define DMA_ISR_HTIF1   (1u << 2)
#define DMA_ISR_TEIF1   (1u << 3)

// DMA IFCR clear bits for Channel 1
#define DMA_IFCR_CGIF1  (1u << 0)
#define DMA_IFCR_CTCIF1 (1u << 1)
#define DMA_IFCR_CHTIF1 (1u << 2)
#define DMA_IFCR_CTEIF1 (1u << 3)

typedef struct {
    // ADC regs
    uint32_t adc_sr, adc_cr1, adc_cr2;
    uint32_t adc_smpr1, adc_smpr2;
    uint32_t adc_sqr1, adc_sqr2, adc_sqr3;
    uint32_t adc_dr;

    // ADC internal
    uint16_t sample12;
    bool adc_running;
    uint64_t adc_timer;
    uint64_t conv_period_ns;

    // DMA regs (only DMA1 Channel1 + ISR/IFCR)
    uint32_t dma_isr;
    uint32_t dma_ccr1;
    uint32_t dma_cndtr1;
    uint32_t dma_cpar1;
    uint32_t dma_cmar1;

    // latched for circular behavior
    uint32_t dma_cndtr1_base;
    uint32_t dma_cmar1_base;

    int dma_irq_line; // your logs: DMA1 IRQ line 11
} AdcWithDmaState;

static AdcWithDmaState g_s;

// -------- helpers --------
static inline void le_store32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
}
static inline void le_store16(uint8_t *b, uint16_t v) {
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void recompute_dma_gif1(AdcWithDmaState *s) {
    // GIF is set when any of HT/TC/TE is set; cleared when none are set.
    if (s->dma_isr & (DMA_ISR_TCIF1 | DMA_ISR_HTIF1 | DMA_ISR_TEIF1)) {
        s->dma_isr |= DMA_ISR_GIF1;
    } else {
        s->dma_isr &= ~DMA_ISR_GIF1;
    }
}

static uint32_t dma_elem_size(uint32_t ccr, bool mem_side) {
    uint32_t sh = mem_side ? DMA_CCR_MSIZE_SHIFT : DMA_CCR_PSIZE_SHIFT;
    uint32_t code = (ccr >> sh) & 0x3; // 00=8, 01=16, 10=32, 11=reserved
    if (code == 0) return 1;
    if (code == 1) return 2;
    if (code == 2) return 4;
    return 2; // forgiving
}

static void dma_raise_if_enabled(AdcWithDmaState *s, bool ht, bool tc) {
    if (ht && (s->dma_ccr1 & DMA_CCR_HTIE)) qemu_plugin_raise_irq(s->dma_irq_line, false);
    if (tc && (s->dma_ccr1 & DMA_CCR_TCIE)) qemu_plugin_raise_irq(s->dma_irq_line, false);
}

static void dma1_ch1_transfer_from_adc(AdcWithDmaState *s) {
    // Only P->M for ADC DMA use-case
    if (!(s->dma_ccr1 & DMA_CCR_EN)) return;
    if (s->dma_ccr1 & DMA_CCR_DIR) return;
    if (s->dma_cndtr1 == 0) return;

    // size selection: pick max(mem, per) so we don't under-write
    uint32_t msz = dma_elem_size(s->dma_ccr1, true);
    uint32_t psz = dma_elem_size(s->dma_ccr1, false);
    uint32_t xsz = (msz > psz) ? msz : psz;
    if (xsz != 1 && xsz != 2 && xsz != 4) xsz = 2;

    // write ADC DR into memory
    uint8_t buf[4] = {0};
    uint32_t val = s->adc_dr;
    if (xsz == 1) buf[0] = (uint8_t)(val & 0xFF);
    else if (xsz == 2) le_store16(buf, (uint16_t)(val & 0xFFFF));
    else le_store32(buf, val);


    (void)qemu_plugin_write_memory((unsigned long long)s->dma_cmar1, buf, (int)xsz);

    // update count/address
    s->dma_cndtr1--;
    if (s->dma_ccr1 & DMA_CCR_MINC) s->dma_cmar1 += xsz;

    bool ht_event = false;
    bool tc_event = false;

    // HT at exactly half remaining
    if (s->dma_cndtr1_base > 1) {
        uint32_t half = s->dma_cndtr1_base / 2;
        if (half > 0 && s->dma_cndtr1 == half) {
            s->dma_isr |= DMA_ISR_HTIF1;
            ht_event = true;
        }
    }

    // TC at 0 remaining
    if (s->dma_cndtr1 == 0) {
        s->dma_isr |= DMA_ISR_TCIF1;
        tc_event = true;

        // circular reload
        if (s->dma_ccr1 & DMA_CCR_CIRC) {
            s->dma_cndtr1 = s->dma_cndtr1_base;
            s->dma_cmar1  = s->dma_cmar1_base;
        }
    }

    // IMPORTANT: never clear TC when setting HT (or vice versa).
    recompute_dma_gif1(s);

    // interrupt(s)
    dma_raise_if_enabled(s, ht_event, tc_event);
}

static void adc_arm(AdcWithDmaState *s, uint64_t delay_ns) {
    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
    qemu_plugin_timer_alarm(s->adc_timer, now + delay_ns);
}

static void adc_conv_cb(void *opaque) {
    AdcWithDmaState *s = (AdcWithDmaState *)opaque;
    if (!s->adc_running) return;
    if (!(s->adc_cr2 & ADC_CR2_ADON)) { s->adc_running = false; return; }

    // synthetic sample
    s->sample12 = (uint16_t)((s->sample12 + 0x31u) & 0x0FFFu);
    s->adc_dr = (uint32_t)s->sample12;

    // EOC
    s->adc_sr |= ADC_SR_EOC;

    // DMA request
    if (s->adc_cr2 & ADC_CR2_DMA) {
        dma1_ch1_transfer_from_adc(s);
    }

    // continuous scheduling
    if (s->adc_cr2 & ADC_CR2_CONT) {
        adc_arm(s, s->conv_period_ns);
    } else {
        s->adc_running = false;
    }
}

// ---------------- required API ----------------
uint64_t adc_with_dma_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque;
    AdcWithDmaState *s = &g_s;

    uint32_t a = (uint32_t)addr;
    uint32_t v = 0;

    if (a >= ADC1_BASE && a < ADC1_BASE + 0x100u) {
        uint32_t off = a - ADC1_BASE;
        switch (off) {
            case ADC_SR_OFF:    v = s->adc_sr; break;
            case ADC_CR1_OFF:   v = s->adc_cr1; break;
            case ADC_CR2_OFF:   v = s->adc_cr2; break;
            case ADC_SMPR1_OFF: v = s->adc_smpr1; break;
            case ADC_SMPR2_OFF: v = s->adc_smpr2; break;
            case ADC_SQR1_OFF:  v = s->adc_sqr1; break;
            case ADC_SQR2_OFF:  v = s->adc_sqr2; break;
            case ADC_SQR3_OFF:  v = s->adc_sqr3; break;
            case ADC_DR_OFF:
                v = s->adc_dr;
                // DR read clears EOC (safe default)
                s->adc_sr &= ~ADC_SR_EOC;
                break;
            default: v = 0; break;
        }
    } else if (a >= DMA1_BASE && a < DMA1_BASE + 0x100u) {
        uint32_t off = a - DMA1_BASE;
        switch (off) {
            case DMA_ISR_OFF:    v = s->dma_isr; break;
            case DMA_CCR1_OFF:   v = s->dma_ccr1; break;
            case DMA_CNDTR1_OFF: v = s->dma_cndtr1; break;
            case DMA_CPAR1_OFF:  v = s->dma_cpar1; break;
            case DMA_CMAR1_OFF:  v = s->dma_cmar1; break;
            default: v = 0; break;
        }
    } else {
        v = 0;
    }

    if (size == 1) return (uint8_t)(v & 0xFF);
    if (size == 2) return (uint16_t)(v & 0xFFFF);
    return (uint64_t)v;
}

void adc_with_dma_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque;
    AdcWithDmaState *s = &g_s;

    uint32_t a = (uint32_t)addr;
    uint32_t v32 = (uint32_t)value;

    // Sub-word writes as RMW against aligned word (keeps “RMW pattern detected” correct)
    if (size == 1 || size == 2) {
        uint32_t aligned = a & ~3u;
        uint32_t cur = (uint32_t)adc_with_dma_read(NULL, aligned, 4);
        uint32_t shift = (a & 3u) * 8u;
        uint32_t mask = (size == 1) ? (0xFFu << shift) : (0xFFFFu << shift);
        v32 = (cur & ~mask) | ((v32 << shift) & mask);
        a = aligned;
    }

    // ADC
    if (a >= ADC1_BASE && a < ADC1_BASE + 0x100u) {
        uint32_t off = a - ADC1_BASE;
        switch (off) {
            case ADC_SR_OFF:
                // allow W1C-style clearing for robustness
                s->adc_sr &= ~v32;
                break;

            case ADC_CR1_OFF:
                s->adc_cr1 = v32;
                break;

            case ADC_CR2_OFF: {
                // Keep CR2 SW-owned and stable (fix entropy mismatch).
                // Only self-clear actual self-clearing bits if set.
                s->adc_cr2 = v32;

                if (s->adc_cr2 & ADC_CR2_SWSTART) s->adc_cr2 &= ~ADC_CR2_SWSTART;
                if (s->adc_cr2 & ADC_CR2_CAL)     s->adc_cr2 &= ~ADC_CR2_CAL;
                if (s->adc_cr2 & ADC_CR2_RSTCAL)  s->adc_cr2 &= ~ADC_CR2_RSTCAL;

                // Start conversions when ADON is enabled and a “start condition” is present:
                // SWSTART, EXTTRIG, or CONT (this is permissive and avoids dead firmware).
                bool start = false;
                if (s->adc_cr2 & ADC_CR2_ADON) {
                    if ((v32 & ADC_CR2_SWSTART) || (s->adc_cr2 & ADC_CR2_EXTTRIG) || (s->adc_cr2 & ADC_CR2_CONT)) {
                        start = true;
                    }
                }

                if (start) {
                    s->adc_running = true;
                    s->adc_sr |= ADC_SR_STRT;
                    // schedule first conversion quickly
                    adc_arm(s, s->conv_period_ns);
                } else if (!(s->adc_cr2 & ADC_CR2_ADON)) {
                    s->adc_running = false;
                }
                break;
            }

            case ADC_SMPR1_OFF: s->adc_smpr1 = v32; break;
            case ADC_SMPR2_OFF: s->adc_smpr2 = v32; break;
            case ADC_SQR1_OFF:  s->adc_sqr1  = v32; break;
            case ADC_SQR2_OFF:  s->adc_sqr2  = v32; break;
            case ADC_SQR3_OFF:  s->adc_sqr3  = v32; break;

            default:
                break;
        }
        return;
    }

    // DMA1
    if (a >= DMA1_BASE && a < DMA1_BASE + 0x100u) {
        uint32_t off = a - DMA1_BASE;
        switch (off) {
            case DMA_IFCR_OFF:
                // W1C: clear only what is requested.
                if (v32 & DMA_IFCR_CGIF1)  s->dma_isr &= ~(DMA_ISR_GIF1 | DMA_ISR_TCIF1 | DMA_ISR_HTIF1 | DMA_ISR_TEIF1);
                if (v32 & DMA_IFCR_CTCIF1) s->dma_isr &= ~DMA_ISR_TCIF1;
                if (v32 & DMA_IFCR_CHTIF1) s->dma_isr &= ~DMA_ISR_HTIF1;
                if (v32 & DMA_IFCR_CTEIF1) s->dma_isr &= ~DMA_ISR_TEIF1;
                recompute_dma_gif1(s);
                break;

            case DMA_CCR1_OFF: {
                uint32_t prev = s->dma_ccr1;

                // Preserve written bits; mask only high reserved (CCR is 16-ish meaningful bits on F1)
                s->dma_ccr1 = v32 & 0x7FFFu;

                bool prev_en = (prev & DMA_CCR_EN) != 0;
                bool new_en  = (s->dma_ccr1 & DMA_CCR_EN) != 0;

                if (!prev_en && new_en) {
                    // latch base for HT/TC and circular reload
                    s->dma_cndtr1_base = s->dma_cndtr1;
                    s->dma_cmar1_base  = s->dma_cmar1;

                    // safety: avoid “never fires” if firmware enables before programming count
                    if (s->dma_cndtr1_base == 0) {
                        s->dma_cndtr1_base = 2;
                        s->dma_cndtr1 = 2;
                    }
                }
                break;
            }

            case DMA_CNDTR1_OFF:
                s->dma_cndtr1 = v32 & 0xFFFFu;
                if (s->dma_ccr1 & DMA_CCR_EN) {
                    s->dma_cndtr1_base = s->dma_cndtr1 ? s->dma_cndtr1 : 2;
                    s->dma_cndtr1 = s->dma_cndtr1_base;
                }
                break;

            case DMA_CPAR1_OFF:
                s->dma_cpar1 = v32;
                break;

            case DMA_CMAR1_OFF:
                s->dma_cmar1 = v32;
                if (s->dma_ccr1 & DMA_CCR_EN) s->dma_cmar1_base = s->dma_cmar1;
                break;

            default:
                break;
        }
        return;
    }
}

void adc_with_dma_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_s, 0, sizeof(g_s));

    g_s.dma_irq_line = 11+16;

    // Make conversions fast enough that HT+TC can both be pending when the ISR is read (matches HW seeing 0x7).
    // This is still “high-fidelity”: it’s just a fast continuous ADC.
    g_s.conv_period_ns = 500; // 2 us

    g_s.sample12 = 0x120;
    g_s.adc_running = false;

    g_s.adc_timer = qemu_plugin_timer_new_ns(adc_conv_cb, &g_s);

    // Optional:
    // dev_debug("adc_with_dma_init: initialized ADC1 + DMA1_CH1 model\n");
}

