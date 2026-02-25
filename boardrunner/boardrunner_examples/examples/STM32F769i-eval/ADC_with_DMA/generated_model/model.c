// Device Model for ADC_with_DMA (STM32F7x9 ADC3 + DMA2 Stream0)
//
// Focus: match observed MMIO programming/polling and produce DMA2 Stream0 TC flag/IRQ behavior.
// Notes:
//  - SWSTART (CR2 bit30) is modeled as self-clearing (trace reads back 0x303 after 0x40000303 write).
//  - DMA2 LISR shows 0x20 after IRQ => TCIF0 (bit5). LIFCR W1C clears flags.
//  - ADC conversions are synthetic; DMA copies ADC3->DR into S0M0AR.
//  - IRQ is raised on the first observed TC event after stream enable, matching trace (single IRQ line).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <device.h>
#include <boardrunner/vio.h>

// ---------------- Base addresses ----------------
#define ADC3_BASE   0x40012200u
#define DMA2_BASE   0x40026400u

// ---------------- ADC3 register offsets ----------------
#define ADC_SR_OFF     0x00u
#define ADC_CR1_OFF    0x04u
#define ADC_CR2_OFF    0x08u
#define ADC_SMPR2_OFF  0x10u
#define ADC_SQR1_OFF   0x2Cu
#define ADC_SQR3_OFF   0x34u
#define ADC_DR_OFF     0x4Cu

// ADC SR bits (compatible with STM32 ADC family usage in trace)
#define ADC_SR_EOC     (1u << 1)
#define ADC_SR_STRT    (1u << 4)

// ADC CR2 bits (STM32F7-style)
#define ADC_CR2_ADON    (1u << 0)
#define ADC_CR2_CONT    (1u << 1)
#define ADC_CR2_DMA     (1u << 8)
#define ADC_CR2_DDS     (1u << 9)
#define ADC_CR2_SWSTART (1u << 30)

// ---------------- DMA2 register offsets (Stream0 + low regs) ----------------
#define DMA_LISR_OFF    0x00u
#define DMA_LIFCR_OFF   0x08u
#define DMA_S0CR_OFF    0x10u
#define DMA_S0NDTR_OFF  0x14u
#define DMA_S0PAR_OFF   0x18u
#define DMA_S0M0AR_OFF  0x1Cu
#define DMA_S0FCR_OFF   0x24u

// DMA SxCR bits (STM32F7)
#define DMA_SxCR_EN      (1u << 0)
#define DMA_SxCR_DMEIE   (1u << 1)
#define DMA_SxCR_TEIE    (1u << 2)
#define DMA_SxCR_HTIE    (1u << 3)
#define DMA_SxCR_TCIE    (1u << 4)
#define DMA_SxCR_DIR_SHIFT 6
#define DMA_SxCR_DIR_MASK  (3u << DMA_SxCR_DIR_SHIFT)  // 00=P2M, 01=M2P, 10=M2M
#define DMA_SxCR_CIRC    (1u << 8)
#define DMA_SxCR_PINC    (1u << 9)
#define DMA_SxCR_MINC    (1u << 10)
#define DMA_SxCR_PSIZE_SHIFT 11
#define DMA_SxCR_MSIZE_SHIFT 13

// DMA LISR bits for Stream0 (low interrupt status)
#define DMA_LISR_FEIF0   (1u << 0)
#define DMA_LISR_DMEIF0  (1u << 2)
#define DMA_LISR_TEIF0   (1u << 3)
#define DMA_LISR_HTIF0   (1u << 4)
#define DMA_LISR_TCIF0   (1u << 5)

// Convenience: mask of all Stream0 low flags
#define DMA_LISR_S0_ALL  (DMA_LISR_FEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_TEIF0 | DMA_LISR_HTIF0 | DMA_LISR_TCIF0)

typedef struct {
    // ---- ADC3 regs ----
    uint32_t adc_sr;
    uint32_t adc_cr1;
    uint32_t adc_cr2;
    uint32_t adc_smpr2;
    uint32_t adc_sqr1;
    uint32_t adc_sqr3;
    uint32_t adc_dr;

    // ---- DMA2 regs (Stream0 + LISR/LIFCR) ----
    uint32_t dma_lisr;   // only the bits we model (stream0 low)
    uint32_t dma_s0cr;
    uint32_t dma_s0ndtr;
    uint32_t dma_s0par;
    uint32_t dma_s0m0ar;
    uint32_t dma_s0fcr;

    // Latched base values for circular behavior (as HW does)
    uint32_t dma_s0ndtr_base;
    uint32_t dma_s0m0ar_base;

    // Internal behavior controls
    uint16_t sample12;
    bool     adc_running;
    uint64_t adc_timer;
    uint64_t conv_period_ns;

    // Trace shows exactly one DMA2 Stream0 interrupt occurrence; latch to avoid storms.
    bool     dma_tc_irq_latched;

    // IRQ line as "vector index" (must be irq+16). Trace says Vector=56.
    int      dma2_stream0_vector; // 56
} AdcDmaState;

static AdcDmaState g_s;

// ---------------- little-endian stores ----------------
static inline void le_store16(uint8_t *b, uint16_t v) {
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)((v >> 8) & 0xFFu);
}
static inline void le_store32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)((v >> 8) & 0xFFu);
    b[2] = (uint8_t)((v >> 16) & 0xFFu);
    b[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t dma_elem_size_from_cr(uint32_t s0cr, bool mem_side) {
    uint32_t sh = mem_side ? DMA_SxCR_MSIZE_SHIFT : DMA_SxCR_PSIZE_SHIFT;
    uint32_t code = (s0cr >> sh) & 0x3u; // 00=8, 01=16, 10=32, 11=reserved
    if (code == 0) return 1;
    if (code == 1) return 2;
    if (code == 2) return 4;
    return 4; // be forgiving
}

static inline uint32_t dma_dir(uint32_t s0cr) {
    return (s0cr & DMA_SxCR_DIR_MASK) >> DMA_SxCR_DIR_SHIFT;
}

// One-shot arm helper
static void adc_arm(AdcDmaState *s, uint64_t delay_ns) {
    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
    qemu_plugin_timer_alarm(s->adc_timer, now + delay_ns);
}

static void dma2_stream0_set_tc_and_irq_once(AdcDmaState *s) {
    // Set TCIF0 in LISR.
    s->dma_lisr |= DMA_LISR_TCIF0;

    // Raise IRQ only once (trace shows a single interrupt occurrence).
    if (!s->dma_tc_irq_latched && (s->dma_s0cr & DMA_SxCR_TCIE)) {
        s->dma_tc_irq_latched = true;
        qemu_plugin_raise_irq(s->dma2_stream0_vector, false);
    }
}

static void dma2_stream0_transfer_from_adc(AdcDmaState *s) {
    // Must be enabled
    if (!(s->dma_s0cr & DMA_SxCR_EN)) return;

    // Direction must be P2M (00)
    if (dma_dir(s->dma_s0cr) != 0) return;

    // Must be configured for ADC DR
    if (s->dma_s0par != (ADC3_BASE + ADC_DR_OFF)) return;

    // If NDTR is zero, in circular mode it reloads from base; otherwise nothing to do.
    if (s->dma_s0ndtr == 0) {
        if (s->dma_s0cr & DMA_SxCR_CIRC) {
            if (s->dma_s0ndtr_base == 0) return;
            s->dma_s0ndtr = s->dma_s0ndtr_base;
            s->dma_s0m0ar = s->dma_s0m0ar_base;
        } else {
            return;
        }
    }

    // Determine transfer size (use max of PSIZE/MSIZE to avoid underwrite)
    uint32_t psz = dma_elem_size_from_cr(s->dma_s0cr, false);
    uint32_t msz = dma_elem_size_from_cr(s->dma_s0cr, true);
    uint32_t xsz = (psz > msz) ? psz : msz;
    if (xsz != 1 && xsz != 2 && xsz != 4) xsz = 4;

    // Write ADC DR into memory
    uint8_t buf[4] = {0,0,0,0};
    uint32_t val = s->adc_dr;

    if (xsz == 1) {
        buf[0] = (uint8_t)(val & 0xFFu);
    } else if (xsz == 2) {
        le_store16(buf, (uint16_t)(val & 0xFFFFu));
    } else {
        le_store32(buf, val);
    }

    (void)qemu_plugin_write_memory((unsigned long long)s->dma_s0m0ar, buf, (int)xsz);

    // Update NDTR/M0AR
    s->dma_s0ndtr--;
    if (s->dma_s0cr & DMA_SxCR_MINC) {
        s->dma_s0m0ar += xsz;
    }

    // On completion (NDTR==0), set TC flag and (one-shot) IRQ; then reload if circular.
    if (s->dma_s0ndtr == 0) {
        dma2_stream0_set_tc_and_irq_once(s);

        if (s->dma_s0cr & DMA_SxCR_CIRC) {
            // Reload for circular mode (HW behavior)
            if (s->dma_s0ndtr_base != 0) {
                s->dma_s0ndtr = s->dma_s0ndtr_base;
                s->dma_s0m0ar = s->dma_s0m0ar_base;
            }
        }
    }
}

// ADC conversion callback (synthetic)
static void adc_conv_cb(void *opaque) {
    AdcDmaState *s = (AdcDmaState *)opaque;

    if (!s->adc_running) return;
    if (!(s->adc_cr2 & ADC_CR2_ADON)) {
        s->adc_running = false;
        return;
    }

    // Produce a synthetic 12-bit sample
    s->sample12 = (uint16_t)((s->sample12 + 0x31u) & 0x0FFFu);
    s->adc_dr = (uint32_t)s->sample12;

    // EOC set (DMA read of DR is treated as consuming it; we clear after DMA transfer)
    s->adc_sr |= ADC_SR_EOC;

    // If ADC DMA is enabled, trigger DMA transfer
    if (s->adc_cr2 & ADC_CR2_DMA) {
        dma2_stream0_transfer_from_adc(s);
        // Treat DMA reading DR as clearing EOC like a DR read would.
        s->adc_sr &= ~ADC_SR_EOC;
    }

    // Continuous scheduling
    if (s->adc_cr2 & ADC_CR2_CONT) {
        adc_arm(s, s->conv_period_ns);
    } else {
        s->adc_running = false;
    }
}

// This function will emulate all device reads
uint64_t adc_with_dma_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque;
    AdcDmaState *s = &g_s;

    uint32_t a = (uint32_t)addr;
    uint32_t v = 0;

    // ADC3 window
    if (a >= ADC3_BASE && a < (ADC3_BASE + 0x100u)) {
        uint32_t off = a - ADC3_BASE;
        switch (off) {
            case ADC_SR_OFF:    v = s->adc_sr; break;
            case ADC_CR1_OFF:   v = s->adc_cr1; break;
            case ADC_CR2_OFF:   v = s->adc_cr2; break;
            case ADC_SMPR2_OFF: v = s->adc_smpr2; break;
            case ADC_SQR1_OFF:  v = s->adc_sqr1; break;
            case ADC_SQR3_OFF:  v = s->adc_sqr3; break;
            case ADC_DR_OFF:
                v = s->adc_dr;
                // CPU read clears EOC in typical STM32 behavior
                s->adc_sr &= ~ADC_SR_EOC;
                break;
            default:
                v = 0;
                break;
        }
    }
    // DMA2 window
    else if (a >= DMA2_BASE && a < (DMA2_BASE + 0x200u)) {
        uint32_t off = a - DMA2_BASE;
        switch (off) {
            case DMA_LISR_OFF:   v = s->dma_lisr; break;
            case DMA_S0CR_OFF:   v = s->dma_s0cr; break;
            case DMA_S0NDTR_OFF: v = s->dma_s0ndtr; break;
            case DMA_S0PAR_OFF:  v = s->dma_s0par; break;
            case DMA_S0M0AR_OFF: v = s->dma_s0m0ar; break;
            case DMA_S0FCR_OFF:  v = s->dma_s0fcr; break;
            default:
                v = 0;
                break;
        }
    } else {
        v = 0;
    }

    if (size == 1) return (uint8_t)(v & 0xFFu);
    if (size == 2) return (uint16_t)(v & 0xFFFFu);
    return (uint64_t)v;
}

// This function will emulate all device writes
void adc_with_dma_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque;
    AdcDmaState *s = &g_s;

    uint32_t a = (uint32_t)addr;
    uint32_t v32 = (uint32_t)value;

    // Sub-word writes as RMW against aligned word
    if (size == 1 || size == 2) {
        uint32_t aligned = a & ~3u;
        uint32_t cur = (uint32_t)adc_with_dma_read(NULL, aligned, 4);
        uint32_t shift = (a & 3u) * 8u;
        uint32_t mask = (size == 1) ? (0xFFu << shift) : (0xFFFFu << shift);
        v32 = (cur & ~mask) | ((v32 << shift) & mask);
        a = aligned;
    }

    // ---------------- ADC3 ----------------
    if (a >= ADC3_BASE && a < (ADC3_BASE + 0x100u)) {
        uint32_t off = a - ADC3_BASE;
        switch (off) {
            case ADC_SR_OFF:
                // W1C-style clearing (trace writes 0xFFFFFFDD)
                s->adc_sr &= ~v32;
                break;

            case ADC_CR1_OFF:
                s->adc_cr1 = v32;
                break;

            case ADC_CR2_OFF: {
                // Self-clearing SWSTART (trace reads back 0x303 after writing 0x40000303)
                bool swstart = (v32 & ADC_CR2_SWSTART) != 0;

                // Store CR2 but clear SWSTART in the stored value
                s->adc_cr2 = v32 & ~ADC_CR2_SWSTART;

                // Start logic:
                // - If ADON is set and SWSTART was written, begin conversions.
                // - Keep running if already running and ADON remains set.
                if (swstart && (s->adc_cr2 & ADC_CR2_ADON)) {
                    s->adc_running = true;
                    s->adc_sr |= ADC_SR_STRT;
                    // Schedule first conversion quickly
                    adc_arm(s, 500); // 0.5us synthetic latency
                }

                // If ADON cleared, stop
                if (!(s->adc_cr2 & ADC_CR2_ADON)) {
                    s->adc_running = false;
                }
                break;
            }

            case ADC_SMPR2_OFF: s->adc_smpr2 = v32; break;
            case ADC_SQR1_OFF:  s->adc_sqr1  = v32; break;
            case ADC_SQR3_OFF:  s->adc_sqr3  = v32; break;

            default:
                break;
        }
        return;
    }

    // ---------------- DMA2 ----------------
    if (a >= DMA2_BASE && a < (DMA2_BASE + 0x200u)) {
        uint32_t off = a - DMA2_BASE;
        switch (off) {
            case DMA_LIFCR_OFF:
                // W1C clear for stream0 low flags (trace uses 0x3F and later 0x20)
                s->dma_lisr &= ~(v32 & DMA_LISR_S0_ALL);

                // Note: We intentionally do NOT reset dma_tc_irq_latched here; trace shows only one IRQ overall.
                // Re-arming happens on EN rising edge.
                break;

            case DMA_S0CR_OFF: {
                uint32_t prev = s->dma_s0cr;
                s->dma_s0cr = v32;

                bool prev_en = (prev & DMA_SxCR_EN) != 0;
                bool new_en  = (s->dma_s0cr & DMA_SxCR_EN) != 0;

                if (!prev_en && new_en) {
                    // Latch base values for circular mode
                    s->dma_s0ndtr_base = s->dma_s0ndtr;
                    s->dma_s0m0ar_base = s->dma_s0m0ar;

                    // If NDTR is 0, make it minimally valid
                    if (s->dma_s0ndtr_base == 0) {
                        s->dma_s0ndtr_base = 1;
                        s->dma_s0ndtr = 1;
                    }

                    // Re-arm the one-shot IRQ behavior on each fresh enable
                    s->dma_tc_irq_latched = false;

                    // Clear pending flags on enable (common driver practice also clears via LIFCR)
                    // but keep it conservative: don't force-clear here if firmware relies on LIFCR.
                }

                // If EN is cleared, stop and allow future re-arm
                if (prev_en && !new_en) {
                    // Nothing else required
                }
                break;
            }

            case DMA_S0NDTR_OFF:
                s->dma_s0ndtr = v32 & 0xFFFFu;
                break;

            case DMA_S0PAR_OFF:
                s->dma_s0par = v32;
                break;

            case DMA_S0M0AR_OFF:
                s->dma_s0m0ar = v32;
                break;

            case DMA_S0FCR_OFF:
                s->dma_s0fcr = v32;
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

    // Trace indicates DMA2 Stream0 interrupt vector = 56 (0x38).
    // Per requirement: pass irq+16; that means we pass the vector index directly as 56.
    g_s.dma2_stream0_vector = 56;

    // DMA2 S0FCR reset-like read observed as 0x21 in trace
    g_s.dma_s0fcr = 0x21u;

    // Synthetic ADC timing
    g_s.conv_period_ns = 5000; // 5us between conversions when CONT=1
    g_s.sample12 = 0x120u;

    g_s.adc_timer = qemu_plugin_timer_new_ns(adc_conv_cb, &g_s);

    // Optional single init log (keep quiet by default)
    // dev_debug("adc_with_dma_init: ADC3 + DMA2 Stream0 model initialized\n");
}
