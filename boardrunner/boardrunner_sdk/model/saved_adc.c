// // // High-fidelity ADC1 (STM32F103xx) model with stable CR2 behavior + ADC->DMA requests.
// // // Goals based on provided data:
// // // - CR2 must have stable readback (no random/reserved/status mixing) to reduce entropy.
// // // - Implement realistic side effects for SWSTART/CAL/RSTCAL (self-clearing / non-sticky).
// // // - Produce DR values + EOC in SR; clear EOC on DR read.
// // // - If CR2.DMA enabled, trigger api_dma_request(stream_id) on each conversion completion.
// // // - Optional: raise ADC IRQ on EOCIE (safe even if not used by firmware).
// // //
// // // NOTE: The ADC->DMA stream mapping is framework-specific.
// // // This model assumes ADC1 uses DMA1 Channel1 stream_id == 0.
// // // If your DMA model registers a different stream id, set ADC1_DMA_STREAM_ID accordingly.

// // #include <device.h>
// // #include <boardrunner/vio.h>

// // #include <stdint.h>
// // #include <stdbool.h>
// // #include <string.h>
// // #include <stdarg.h>
// // #include <stdio.h>

// // #ifndef ADC1_BASE_ADDR
// // #define ADC1_BASE_ADDR 0x40012400ULL
// // #endif

// // #ifndef ADC1_DMA_STREAM_ID
// // #define ADC1_DMA_STREAM_ID 0
// // #endif

// // #ifndef ADC1_IRQ
// // // STM32F103: ADC1_2 interrupt is typically NVIC line 18.
// // #define ADC1_IRQ 18+16
// // #endif

// // // ADC register offsets
// // #define ADC_SR_OFF     0x00
// // #define ADC_CR1_OFF    0x04
// // #define ADC_CR2_OFF    0x08
// // #define ADC_SMPR1_OFF  0x0C
// // #define ADC_SMPR2_OFF  0x10
// // #define ADC_SQR1_OFF   0x2C
// // #define ADC_SQR2_OFF   0x30
// // #define ADC_SQR3_OFF   0x34
// // #define ADC_DR_OFF     0x4C

// // // SR bits (subset)
// // #define ADC_SR_EOC     (1u << 1)
// // #define ADC_SR_STRT    (1u << 4)

// // // CR1 bits (subset)
// // #define ADC_CR1_EOCIE  (1u << 5)

// // // CR2 bits (subset; STM32F1-style)
// // #define ADC_CR2_ADON    (1u << 0)
// // #define ADC_CR2_CONT    (1u << 1)
// // #define ADC_CR2_CAL     (1u << 2)
// // #define ADC_CR2_RSTCAL  (1u << 3)
// // #define ADC_CR2_DMA     (1u << 8)
// // #define ADC_CR2_ALIGN   (1u << 11)
// // #define ADC_CR2_EXTTRIG (1u << 20)
// // #define ADC_CR2_SWSTART (1u << 22)

// // // Mask of "meaningful" CR2 bits we model (keep stable; do not inject randomness)
// // #define ADC_CR2_MODELED_MASK ( \
// //     ADC_CR2_ADON | ADC_CR2_CONT | ADC_CR2_CAL | ADC_CR2_RSTCAL | ADC_CR2_DMA | \
// //     ADC_CR2_ALIGN | ADC_CR2_EXTTRIG | ADC_CR2_SWSTART | \
// //     /* EXTSEL bits [17:19] are heavily used in your traces (0xE0000 / 0x1E0000) */ \
// //     (0x7u << 17) )

// // // Timer behavior
// // #define NS_PER_US 1000ULL

// // typedef struct ADC1State {
// //     uint32_t sr;
// //     uint32_t cr1;
// //     uint32_t cr2;
// //     uint32_t smpr1;
// //     uint32_t smpr2;
// //     uint32_t sqr1;
// //     uint32_t sqr2;
// //     uint32_t sqr3;

// //     uint16_t dr;            // 12-bit raw, stored in lower bits
// //     uint16_t sample_counter;

// //     // one-shot timers
// //     uint64_t t_conv;        // conversion completion
// //     uint64_t t_cal;         // calibration completion

// //     bool powered;           // ADON seen
// //     bool converting;        // conversion in flight
// // } ADC1State;

// // static ADC1State g_adc1;

// // // ---- small debug helper
// // static void dbg(const char *fmt, ...) {
// //     char buf[256];
// //     va_list ap;
// //     va_start(ap, fmt);
// //     vsnprintf(buf, sizeof(buf), fmt, ap);
// //     va_end(ap);
// //     dev_debug(buf);
// // }

// // // ---- partial access helpers
// // static inline uint32_t read_slice32(uint32_t v, hwaddr off, unsigned size) {
// //     if (size == 4) return v;
// //     if (size == 2) {
// //         unsigned sh = (unsigned)((off & 2u) * 8u);
// //         return (v >> sh) & 0xFFFFu;
// //     }
// //     unsigned sh = (unsigned)((off & 3u) * 8u);
// //     return (v >> sh) & 0xFFu;
// // }

// // static inline uint32_t merge_write32(uint32_t oldv, uint64_t value, hwaddr off, unsigned size) {
// //     if (size == 4) return (uint32_t)value;
// //     if (size == 2) {
// //         unsigned sh = (unsigned)((off & 2u) * 8u);
// //         uint32_t mask = 0xFFFFu << sh;
// //         return (oldv & ~mask) | (((uint32_t)value & 0xFFFFu) << sh);
// //     }
// //     unsigned sh = (unsigned)((off & 3u) * 8u);
// //     uint32_t mask = 0xFFu << sh;
// //     return (oldv & ~mask) | (((uint32_t)value & 0xFFu) << sh);
// // }

// // static inline hwaddr adc_off(hwaddr addr) {
// //     uint64_t a = (uint64_t)addr;
// //     if (a >= ADC1_BASE_ADDR) return (hwaddr)(a - ADC1_BASE_ADDR);
// //     return addr;
// // }

// // // ---- channel selection (minimal): first regular conversion is in SQR3[4:0]
// // static inline uint8_t adc_regular_channel0(void) {
// //     return (uint8_t)(g_adc1.sqr3 & 0x1Fu);
// // }

// // static uint16_t adc_generate_sample(uint8_t ch) {
// //     // Deterministic but changing, keeps firmware "alive".
// //     // 12-bit sample: mix channel + counter.
// //     g_adc1.sample_counter++;
// //     uint16_t v = (uint16_t)((ch * 131u) + g_adc1.sample_counter);
// //     return (uint16_t)(v & 0x0FFFu);
// // }

// // static void adc_finish_conversion(void *opaque);

// // static void adc_start_conversion(void) {
// //     if (!g_adc1.powered) return;
// //     if (g_adc1.converting) return;

// //     g_adc1.converting = true;
// //     g_adc1.sr |= ADC_SR_STRT;

// //     // Hardware behavior: SWSTART is not a stable "config" bit; clear it once conversion starts.
// //     g_adc1.cr2 &= ~ADC_CR2_SWSTART;

// //     // Schedule conversion completion shortly in the future.
// //     // (Absolute timestamp required by qemu_plugin_timer_alarm)
// //     int64_t now = qemu_plugin_get_virtual_timer();
// //     uint64_t deadline = (uint64_t)now + (50ULL * NS_PER_US); // 50us nominal
// //     qemu_plugin_timer_alarm(g_adc1.t_conv, deadline);
// // }

// // static void adc_request_dma_if_enabled(void) {
// //     if (g_adc1.cr2 & ADC_CR2_DMA) {
// //         api_dma_request(ADC1_DMA_STREAM_ID);
// //     }
// // }

// // static void adc_finish_conversion(void *opaque) {
// //     (void)opaque;

// //     // If ADC got powered off mid-flight, just stop.
// //     if (!g_adc1.powered) {
// //         g_adc1.converting = false;
// //         g_adc1.sr &= ~(ADC_SR_STRT | ADC_SR_EOC);
// //         return;
// //     }

// //     uint8_t ch = adc_regular_channel0();
// //     g_adc1.dr = adc_generate_sample(ch);

// //     // Set EOC; clear STRT to indicate done.
// //     g_adc1.sr |= ADC_SR_EOC;
// //     g_adc1.sr &= ~ADC_SR_STRT;
// //     g_adc1.converting = false;

// //     // DMA request on each completed conversion if enabled
// //     adc_request_dma_if_enabled();

// //     // Optional IRQ if EOCIE enabled
// //     if (g_adc1.cr1 & ADC_CR1_EOCIE) {
// //         qemu_plugin_raise_irq(ADC1_IRQ, false);
// //     }

// //     // Continuous mode: immediately start another conversion
// //     if (g_adc1.cr2 & ADC_CR2_CONT) {
// //         adc_start_conversion();
// //     }
// // }

// // static void adc_finish_calibration(void *opaque) {
// //     (void)opaque;
// //     // CAL and RSTCAL are self-clearing
// //     g_adc1.cr2 &= ~(ADC_CR2_CAL | ADC_CR2_RSTCAL);
// // }

// // // ---- MMIO read/write
// // uint64_t adc1_read(void *opaque, hwaddr addr, unsigned size) {
// //     (void)opaque;
// //     hwaddr off = adc_off(addr);
// //     hwaddr reg = off & ~3u;

// //     uint32_t v = 0;
// //     switch (reg) {
// //         case ADC_SR_OFF:    v = g_adc1.sr; break;
// //         case ADC_CR1_OFF:   v = g_adc1.cr1; break;
// //         case ADC_CR2_OFF:
// //             // Stable CR2: return only modeled bits (no injected randomness).
// //             v = g_adc1.cr2 & ADC_CR2_MODELED_MASK;
// //             break;
// //         case ADC_SMPR1_OFF: v = g_adc1.smpr1; break;
// //         case ADC_SMPR2_OFF: v = g_adc1.smpr2; break;
// //         case ADC_SQR1_OFF:  v = g_adc1.sqr1; break;
// //         case ADC_SQR2_OFF:  v = g_adc1.sqr2; break;
// //         case ADC_SQR3_OFF:  v = g_adc1.sqr3; break;
// //         case ADC_DR_OFF: {
// //             // Reading DR clears EOC (common firmware assumption).
// //             // Apply alignment: if ALIGN=1 => left align in 16-bit (put in bits[15:4]).
// //             uint16_t raw = (uint16_t)(g_adc1.dr & 0x0FFFu);
// //             uint16_t out = (g_adc1.cr2 & ADC_CR2_ALIGN) ? (uint16_t)(raw << 4) : raw;

// //             // Clear EOC on DR read
// //             g_adc1.sr &= ~ADC_SR_EOC;

// //             // return as 32-bit in low bits
// //             v = (uint32_t)out;
// //             break;
// //         }
// //         default:
// //             v = 0;
// //             break;
// //     }

// //     return (uint64_t)read_slice32(v, off, size);
// // }

// // void adc1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
// //     (void)opaque;
// //     hwaddr off = adc_off(addr);
// //     hwaddr reg = off & ~3u;

// //     switch (reg) {
// //         case ADC_SR_OFF: {
// //             // Some firmwares clear flags by writing 0. We'll support W0C for EOC.
// //             uint32_t w = merge_write32(g_adc1.sr, value, off, size);
// //             if ((w & ADC_SR_EOC) == 0) g_adc1.sr &= ~ADC_SR_EOC;
// //             if ((w & ADC_SR_STRT) == 0) g_adc1.sr &= ~ADC_SR_STRT;
// //             break;
// //         }

// //         case ADC_CR1_OFF:
// //             g_adc1.cr1 = merge_write32(g_adc1.cr1, value, off, size);
// //             break;

// //         case ADC_CR2_OFF: {
// //             uint32_t old = g_adc1.cr2;
// //             uint32_t newv = merge_write32(old, value, off, size);

// //             // Keep only modeled bits stable; drop unknown/reserved.
// //             newv &= ADC_CR2_MODELED_MASK;

// //             // Power control via ADON
// //             bool old_adon = (old & ADC_CR2_ADON) != 0;
// //             bool new_adon = (newv & ADC_CR2_ADON) != 0;

// //             if (!old_adon && new_adon) {
// //                 g_adc1.powered = true;
// //             } else if (old_adon && !new_adon) {
// //                 // Power off: stop ongoing conversion, clear status bits that confuse firmware loops.
// //                 g_adc1.powered = false;
// //                 g_adc1.converting = false;
// //                 g_adc1.sr &= ~(ADC_SR_EOC | ADC_SR_STRT);
// //                 // On real HW, config bits can remain; we keep them as written (except status is separate).
// //             }

// //             // Handle calibration bits (self-clearing)
// //             if ((newv & ADC_CR2_RSTCAL) || (newv & ADC_CR2_CAL)) {
// //                 int64_t now = qemu_plugin_get_virtual_timer();
// //                 uint64_t deadline = (uint64_t)now + (200ULL * NS_PER_US); // 200us nominal
// //                 qemu_plugin_timer_alarm(g_adc1.t_cal, deadline);
// //             }

// //             // SWSTART: if set and powered, start conversion; clear SWSTART when starting
// //             bool swstart_set = (newv & ADC_CR2_SWSTART) != 0;
// //             g_adc1.cr2 = newv;

// //             if (swstart_set && g_adc1.powered) {
// //                 adc_start_conversion();
// //             }
// //             break;
// //         }

// //         case ADC_SMPR1_OFF:
// //             g_adc1.smpr1 = merge_write32(g_adc1.smpr1, value, off, size);
// //             break;

// //         case ADC_SMPR2_OFF:
// //             g_adc1.smpr2 = merge_write32(g_adc1.smpr2, value, off, size);
// //             break;

// //         case ADC_SQR1_OFF:
// //             g_adc1.sqr1 = merge_write32(g_adc1.sqr1, value, off, size);
// //             break;

// //         case ADC_SQR2_OFF:
// //             g_adc1.sqr2 = merge_write32(g_adc1.sqr2, value, off, size);
// //             break;

// //         case ADC_SQR3_OFF:
// //             g_adc1.sqr3 = merge_write32(g_adc1.sqr3, value, off, size);
// //             break;

// //         case ADC_DR_OFF:
// //             // DR is read-mostly; ignore writes
// //             break;

// //         default:
// //             break;
// //     }
// // }

// // void adc1_init(ConfigSection* model_info) {
// //     (void)model_info;
// //     memset(&g_adc1, 0, sizeof(g_adc1));

// //     // Create timers (one-shot)
// //     g_adc1.t_conv = qemu_plugin_timer_new_ns(adc_finish_conversion, &g_adc1);
// //     g_adc1.t_cal  = qemu_plugin_timer_new_ns(adc_finish_calibration, &g_adc1);

// //     // Reset state (CR2=0 is important to match the observed "read 0 then write 0xE0002" behavior)
// //     g_adc1.sr = 0;
// //     g_adc1.cr1 = 0;
// //     g_adc1.cr2 = 0;
// //     g_adc1.smpr1 = 0;
// //     g_adc1.smpr2 = 0;
// //     g_adc1.sqr1 = 0;
// //     g_adc1.sqr2 = 0;
// //     g_adc1.sqr3 = 0;
// //     g_adc1.dr = 0;
// //     g_adc1.sample_counter = 0;
// //     g_adc1.powered = false;
// //     g_adc1.converting = false;

// //     // Optional:
// //     // dbg("ADC1 init: dma_stream=%d irq=%d\n", ADC1_DMA_STREAM_ID, ADC1_IRQ);
// // }


// // adc_with_dma.c
// // Device Model for adc_with_dma (STM32F103 ADC1 + DMA1 Channel1)
// // Focus: correct DMA ISR flag semantics (HTIF/TCIF/GIF), IFCR W1C,
// //        and enough ADC activity to drive DMA counter to half/complete.
// //
// // Includes <device.h> and <boardrunner/vio.h> to access APIs listed by user.

// #include <stdint.h>
// #include <stdbool.h>
// #include <stdlib.h>
// #include <string.h>

// #include <device.h>
// #include <boardrunner/vio.h>

// // -------------------- Base addresses (absolute) --------------------
// #define ADC1_BASE       0x40012400u
// #define DMA1_BASE       0x40020000u

// // ADC1 offsets
// #define ADC_SR_OFF      0x00u
// #define ADC_CR1_OFF     0x04u
// #define ADC_CR2_OFF     0x08u
// #define ADC_SMPR1_OFF   0x0Cu
// #define ADC_SMPR2_OFF   0x10u
// #define ADC_SQR1_OFF    0x2Cu
// #define ADC_SQR2_OFF    0x30u
// #define ADC_SQR3_OFF    0x34u
// #define ADC_DR_OFF      0x4Cu

// #define ADC1_DR_ADDR    (ADC1_BASE + ADC_DR_OFF)

// // ADC SR bits (subset)
// #define ADC_SR_EOC      (1u << 1)

// // ADC CR2 bits (subset, STM32F1-style)
// #define ADC_CR2_ADON    (1u << 0)
// #define ADC_CR2_CONT    (1u << 1)
// #define ADC_CR2_CAL     (1u << 2)
// #define ADC_CR2_RSTCAL  (1u << 3)
// #define ADC_CR2_DMA     (1u << 8)
// #define ADC_CR2_EXTTRIG (1u << 20)
// #define ADC_CR2_SWSTART (1u << 22)

// // DMA offsets
// #define DMA_ISR_OFF     0x00u
// #define DMA_IFCR_OFF    0x04u
// // Channel1
// #define DMA_CCR1_OFF    0x08u
// #define DMA_CNDTR1_OFF  0x0Cu
// #define DMA_CPAR1_OFF   0x10u
// #define DMA_CMAR1_OFF   0x14u

// // DMA CCR bits (subset)
// #define DMA_CCR_EN      (1u << 0)
// #define DMA_CCR_TCIE    (1u << 1)
// #define DMA_CCR_HTIE    (1u << 2)
// #define DMA_CCR_TEIE    (1u << 3)
// #define DMA_CCR_DIR     (1u << 4)   // 0 = P->M, 1 = M->P
// #define DMA_CCR_CIRC    (1u << 5)
// #define DMA_CCR_PINC    (1u << 6)
// #define DMA_CCR_MINC    (1u << 7)
// #define DMA_CCR_PSIZE_SHIFT 8
// #define DMA_CCR_MSIZE_SHIFT 10

// // DMA1 ISR bits for Channel1
// #define DMA_ISR_GIF1    (1u << 0)
// #define DMA_ISR_TCIF1   (1u << 1)
// #define DMA_ISR_HTIF1   (1u << 2)
// #define DMA_ISR_TEIF1   (1u << 3)

// typedef struct {
//     // ADC regs
//     uint32_t adc_sr;
//     uint32_t adc_cr1;
//     uint32_t adc_cr2;
//     uint32_t adc_smpr1;
//     uint32_t adc_smpr2;
//     uint32_t adc_sqr1;
//     uint32_t adc_sqr2;
//     uint32_t adc_sqr3;
//     uint16_t adc_dr;       // 16-bit result, stored in lower bits

//     // DMA regs (DMA1 Channel1 only)
//     uint32_t dma_isr;      // contains CH1 flags in bits [3:0]
//     uint32_t dma_ccr1;
//     uint32_t dma_cndtr1;
//     uint32_t dma_cpar1;
//     uint32_t dma_cmar1;

//     // Internal DMA bookkeeping
//     uint32_t ch1_reload_count;   // latched initial count (for circular + HT threshold)
//     uint32_t ch1_half_rem;       // threshold in "remaining" units
//     uint32_t ch1_cur_maddr;
//     uint32_t ch1_cur_paddr;

//     // Timers
//     uint64_t adc_periodic_timer;
//     uint64_t adc_cal_timer;

//     // Simple sample generator
//     uint16_t sample;

//     // IRQ line (DMA1_Channel1_IRQn is 11 on STM32F103)
//     int dma1_ch1_irq;

// } ADC_DMA_State;

// static ADC_DMA_State g;

// // -------------------- Helpers --------------------
// static inline uint64_t mask_by_size(unsigned size) {
//     if (size >= 8) return 0xFFFFFFFFFFFFFFFFull;
//     return (1ull << (size * 8)) - 1ull;
// }

// // Merge partial writes into 32-bit register (handles 8/16/32-bit MMIO writes)
// static inline void reg_write32_masked(uint32_t *reg, hwaddr addr, uint64_t value, unsigned size) {
//     uint32_t shift = (uint32_t)((addr & 3u) * 8u);
//     uint32_t m = (uint32_t)(mask_by_size(size) << shift);
//     uint32_t v = (uint32_t)((value & mask_by_size(size)) << shift);
//     *reg = (*reg & ~m) | (v & m);
// }

// static inline uint64_t reg_read32_sized(uint32_t reg, hwaddr addr, unsigned size) {
//     uint32_t shift = (uint32_t)((addr & 3u) * 8u);
//     uint32_t v = (reg >> shift);
//     return (uint64_t)(v & (uint32_t)mask_by_size(size));
// }

// static void dma1_recompute_gif1(void) {
//     // GIF1 is set if any of TC/HT/TE flags are set.
//     uint32_t any = g.dma_isr & (DMA_ISR_TCIF1 | DMA_ISR_HTIF1 | DMA_ISR_TEIF1);
//     if (any) g.dma_isr |= DMA_ISR_GIF1;
//     else     g.dma_isr &= ~DMA_ISR_GIF1;
// }

// static void dma1_set_flag(uint32_t flag) {
//     uint32_t before = g.dma_isr;
//     g.dma_isr |= flag;
//     dma1_recompute_gif1();

//     // Raise IRQ only on rising edge of relevant flags when interrupts enabled.
//     // This isn’t perfect level-sensitive modeling, but it matches typical handler behavior.
//     if (!(before & flag)) {
//         if ((flag == DMA_ISR_TCIF1) && (g.dma_ccr1 & DMA_CCR_TCIE)) {
//             qemu_plugin_raise_irq(g.dma1_ch1_irq, false);
//         } else if ((flag == DMA_ISR_HTIF1) && (g.dma_ccr1 & DMA_CCR_HTIE)) {
//             qemu_plugin_raise_irq(g.dma1_ch1_irq, false);
//         } else if ((flag == DMA_ISR_TEIF1) && (g.dma_ccr1 & DMA_CCR_TEIE)) {
//             qemu_plugin_raise_irq(g.dma1_ch1_irq, false);
//         }
//     }
// }

// static void dma1_ifcr_write(uint32_t value) {
//     // IFCR is W1C. Only CH1 low nibble modeled here.
//     // Writing 1 clears corresponding ISR flag bits.
//     if (value & DMA_ISR_GIF1)  g.dma_isr &= ~DMA_ISR_GIF1;   // clearing GIF1 alone is allowed via IFCR in HW layout
//     if (value & DMA_ISR_TCIF1) g.dma_isr &= ~DMA_ISR_TCIF1;
//     if (value & DMA_ISR_HTIF1) g.dma_isr &= ~DMA_ISR_HTIF1;
//     if (value & DMA_ISR_TEIF1) g.dma_isr &= ~DMA_ISR_TEIF1;

//     // After individual clears, recompute GIF1 from remaining flags.
//     dma1_recompute_gif1();
// }

// static inline uint32_t dma_ccr_xsize(uint32_t ccr, uint32_t shift) {
//     return (ccr >> shift) & 0x3u; // 0=8b, 1=16b, 2=32b (per STM32)
// }

// static inline uint32_t dma_size_bytes(uint32_t enc) {
//     if (enc == 0) return 1;
//     if (enc == 1) return 2;
//     return 4;
// }

// // Perform a single DMA request for CH1 (as if ADC triggered it).
// static void dma1_ch1_request_from_adc(uint16_t adc_value) {
//     // Must be enabled
//     if (!(g.dma_ccr1 & DMA_CCR_EN)) return;

//     // Direction must be P->M for ADC usage
//     if (g.dma_ccr1 & DMA_CCR_DIR) {
//         // Wrong direction for ADC; model TEIF1 and IRQ if enabled.
//         dma1_set_flag(DMA_ISR_TEIF1);
//         return;
//     }

//     // If count is zero, hardware would normally have already completed; in circular, it reloads.
//     if (g.dma_cndtr1 == 0) {
//         if (g.dma_ccr1 & DMA_CCR_CIRC) {
//             if (g.ch1_reload_count == 0) return; // nothing to do
//             g.dma_cndtr1 = g.ch1_reload_count;
//             g.ch1_cur_maddr = g.dma_cmar1;
//             g.ch1_cur_paddr = g.dma_cpar1;
//         } else {
//             return;
//         }
//     }

//     // Determine element sizes
//     uint32_t psize = dma_size_bytes(dma_ccr_xsize(g.dma_ccr1, DMA_CCR_PSIZE_SHIFT));
//     uint32_t msize = dma_size_bytes(dma_ccr_xsize(g.dma_ccr1, DMA_CCR_MSIZE_SHIFT));
//     // For ADC, typically both are 2 bytes.
//     uint32_t xfer = (psize > msize) ? psize : msize;

//     // Source peripheral address: usually ADC1_DR_ADDR
//     uint32_t paddr = g.ch1_cur_paddr;
//     (void)paddr;

//     // Destination memory address
//     uint32_t maddr = g.ch1_cur_maddr;

//     // Write conversion data into memory (respect xfer size)
//     uint8_t buf[4] = {0};
//     // ADC is 12-bit right-aligned by default; store as 16-bit or 32-bit depending on config
//     uint32_t payload = (uint32_t)(adc_value & 0x0FFFu);
//     if (xfer == 1) {
//         buf[0] = (uint8_t)(payload & 0xFFu);
//     } else if (xfer == 2) {
//         buf[0] = (uint8_t)(payload & 0xFFu);
//         buf[1] = (uint8_t)((payload >> 8) & 0xFFu);
//     } else {
//         buf[0] = (uint8_t)(payload & 0xFFu);
//         buf[1] = (uint8_t)((payload >> 8) & 0xFFu);
//         buf[2] = (uint8_t)((payload >> 16) & 0xFFu);
//         buf[3] = (uint8_t)((payload >> 24) & 0xFFu);
//     }
//     (void)qemu_plugin_write_memory((unsigned long long)maddr, buf, (int)xfer);

//     // Update internal pointers
//     if (g.dma_ccr1 & DMA_CCR_MINC) g.ch1_cur_maddr += xfer;
//     if (g.dma_ccr1 & DMA_CCR_PINC) g.ch1_cur_paddr += xfer;

//     // Decrement remaining count
//     g.dma_cndtr1--;

//     // Half-transfer flag when remaining reaches half of initial (classic STM32 behavior).
//     // If initial count is 0 (not latched), derive it from first observed count + 1.
//     if (g.ch1_reload_count == 0) {
//         // Best-effort latch (only if firmware enabled without prior CNDTR programming visibility)
//         g.ch1_reload_count = g.dma_cndtr1 + 1;
//         g.ch1_half_rem = g.ch1_reload_count / 2;
//     }

//     if (g.dma_cndtr1 == g.ch1_half_rem) {
//         dma1_set_flag(DMA_ISR_HTIF1);
//     }

//     // Transfer complete flag when count hits 0
//     if (g.dma_cndtr1 == 0) {
//         dma1_set_flag(DMA_ISR_TCIF1);

//         // In circular mode, reload NDTR and restart pointers
//         if (g.dma_ccr1 & DMA_CCR_CIRC) {
//             if (g.ch1_reload_count != 0) {
//                 g.dma_cndtr1 = g.ch1_reload_count;
//                 g.ch1_cur_maddr = g.dma_cmar1;
//                 g.ch1_cur_paddr = g.dma_cpar1;
//             }
//         } else {
//             // Disable channel in non-circular completion? STM32 typically leaves EN set,
//             // but no further requests progress because NDTR==0 unless software reloads.
//             // We'll keep EN as-is to preserve firmware-visible CCR1 reads.
//         }
//     }
// }

// // -------------------- ADC behavior --------------------
// static bool adc_should_run(void) {
//     if (!(g.adc_cr2 & ADC_CR2_ADON)) return false;

//     // If EXTTRIG selected and SWSTART used, SWSTART triggers.
//     // If CONT set and ADC is enabled, we keep running once started.
//     // We'll consider "running" if SWSTART was set at least once and not single-shot completed.
//     // Simplify: run when ADON and (CONT or SWSTART) and DMA is enabled (since this is adc_with_dma).
//     if (g.adc_cr2 & ADC_CR2_SWSTART) return true;
//     if (g.adc_cr2 & ADC_CR2_CONT)    return true;
//     return false;
// }

// static void adc_start_conversion_if_triggered(uint32_t old_cr2) {
//     // If SWSTART is newly set, start conversion and self-clear SWSTART (hardware clears on start)
//     bool swstart_rise = (!(old_cr2 & ADC_CR2_SWSTART)) && (g.adc_cr2 & ADC_CR2_SWSTART);
//     if (swstart_rise && (g.adc_cr2 & ADC_CR2_ADON)) {
//         // Clear SWSTART immediately to mimic HW (so later reads often do not observe it)
//         g.adc_cr2 &= ~ADC_CR2_SWSTART;
//     }
// }

// static void adc_cal_cb(void *opaque) {
//     (void)opaque;
//     // Clear CAL and RSTCAL once timer fires
//     g.adc_cr2 &= ~(ADC_CR2_CAL | ADC_CR2_RSTCAL);
// }

// static void adc_schedule_cal_clear(uint64_t delay_ns) {
//     int64_t now = qemu_plugin_get_virtual_timer();
//     uint64_t when = (uint64_t)now + delay_ns;
//     qemu_plugin_timer_alarm(g.adc_cal_timer, when);
// }

// static void adc_periodic_cb(void *opaque) {
//     (void)opaque;

//     if (!adc_should_run()) return;

//     // Generate a deterministic "analog" value (12-bit)
//     g.sample = (uint16_t)((g.sample + 0x0137u) & 0x0FFFu);
//     g.adc_dr = g.sample;

//     // Set EOC
//     g.adc_sr |= ADC_SR_EOC;

//     // If DMA enabled, request DMA transfer (Channel1)
//     if (g.adc_cr2 & ADC_CR2_DMA) {
//         dma1_ch1_request_from_adc(g.adc_dr);
//     }

//     // If not continuous, stop after one conversion.
//     if (!(g.adc_cr2 & ADC_CR2_CONT)) {
//         // leave ADON set, but stop producing conversions unless SWSTART asserted again
//         // (firmware typically uses SWSTART to re-trigger)
//         // We also clear EOC only when DR is read, like hardware.
//     }
// }

// // -------------------- MMIO read/write --------------------
// static uint64_t adc1_read(hwaddr addr, unsigned size) {
//     uint32_t off = (uint32_t)(addr - ADC1_BASE);
//     switch (off) {
//         case ADC_SR_OFF:    return reg_read32_sized(g.adc_sr, addr, size);
//         case ADC_CR1_OFF:   return reg_read32_sized(g.adc_cr1, addr, size);
//         case ADC_CR2_OFF:   return reg_read32_sized(g.adc_cr2, addr, size);
//         case ADC_SMPR1_OFF: return reg_read32_sized(g.adc_smpr1, addr, size);
//         case ADC_SMPR2_OFF: return reg_read32_sized(g.adc_smpr2, addr, size);
//         case ADC_SQR1_OFF:  return reg_read32_sized(g.adc_sqr1, addr, size);
//         case ADC_SQR2_OFF:  return reg_read32_sized(g.adc_sqr2, addr, size);
//         case ADC_SQR3_OFF:  return reg_read32_sized(g.adc_sqr3, addr, size);
//         case ADC_DR_OFF: {
//             // Reading DR returns result; clears EOC.
//             uint32_t dr32 = (uint32_t)g.adc_dr;
//             g.adc_sr &= ~ADC_SR_EOC;
//             return reg_read32_sized(dr32, addr, size);
//         }
//         default:
//             return 0;
//     }
// }

// static void adc1_write(hwaddr addr, uint64_t value, unsigned size) {
//     uint32_t off = (uint32_t)(addr - ADC1_BASE);

//     switch (off) {
//         case ADC_SR_OFF:
//             // Usually status bits are cleared by reading DR/SR sequences; allow W1C-like clear for robustness:
//             // writing 0 clears writable bits, writing 1 has no effect (best-effort).
//             // We only model EOC.
//             if ((value & ADC_SR_EOC) == 0) g.adc_sr &= ~ADC_SR_EOC;
//             break;

//         case ADC_CR1_OFF:
//             reg_write32_masked(&g.adc_cr1, addr, value, size);
//             break;

//         case ADC_CR2_OFF: {
//             uint32_t old = g.adc_cr2;
//             reg_write32_masked(&g.adc_cr2, addr, value, size);

//             // Handle calibration bits as self-clearing operations
//             if ((g.adc_cr2 & ADC_CR2_RSTCAL) && !(old & ADC_CR2_RSTCAL)) {
//                 adc_schedule_cal_clear(2000); // ~2us (virtual), best-effort
//             }
//             if ((g.adc_cr2 & ADC_CR2_CAL) && !(old & ADC_CR2_CAL)) {
//                 adc_schedule_cal_clear(5000); // ~5us (virtual), best-effort
//             }

//             // SWSTART triggers conversion and self-clears
//             adc_start_conversion_if_triggered(old);
//             break;
//         }

//         case ADC_SMPR1_OFF:
//             reg_write32_masked(&g.adc_smpr1, addr, value, size);
//             break;
//         case ADC_SMPR2_OFF:
//             reg_write32_masked(&g.adc_smpr2, addr, value, size);
//             break;
//         case ADC_SQR1_OFF:
//             reg_write32_masked(&g.adc_sqr1, addr, value, size);
//             break;
//         case ADC_SQR2_OFF:
//             reg_write32_masked(&g.adc_sqr2, addr, value, size);
//             break;
//         case ADC_SQR3_OFF:
//             reg_write32_masked(&g.adc_sqr3, addr, value, size);
//             break;

//         case ADC_DR_OFF:
//             // DR is read-only in normal operation; ignore writes.
//             break;

//         default:
//             break;
//     }
// }

// static uint64_t dma1_read(hwaddr addr, unsigned size) {
//     uint32_t off = (uint32_t)(addr - DMA1_BASE);
//     switch (off) {
//         case DMA_ISR_OFF:    return reg_read32_sized(g.dma_isr, addr, size);
//         case DMA_CCR1_OFF:   return reg_read32_sized(g.dma_ccr1, addr, size);
//         case DMA_CNDTR1_OFF: return reg_read32_sized(g.dma_cndtr1, addr, size);
//         case DMA_CPAR1_OFF:  return reg_read32_sized(g.dma_cpar1, addr, size);
//         case DMA_CMAR1_OFF:  return reg_read32_sized(g.dma_cmar1, addr, size);
//         // IFCR is write-only; reads usually return 0
//         case DMA_IFCR_OFF:   return 0;
//         default:
//             return 0;
//     }
// }

// static void dma1_write(hwaddr addr, uint64_t value, unsigned size) {
//     uint32_t off = (uint32_t)(addr - DMA1_BASE);

//     switch (off) {
//         case DMA_IFCR_OFF: {
//             // Only 32-bit meaningful, but accept sized writes.
//             uint32_t v = (uint32_t)(value & (uint32_t)mask_by_size(size));
//             dma1_ifcr_write(v);
//             break;
//         }

//         case DMA_CCR1_OFF: {
//             uint32_t old = g.dma_ccr1;
//             reg_write32_masked(&g.dma_ccr1, addr, value, size);

//             bool en_rise = (!(old & DMA_CCR_EN)) && (g.dma_ccr1 & DMA_CCR_EN);
//             if (en_rise) {
//                 // Latch reload count/half threshold at enable time
//                 if (g.dma_cndtr1 != 0) {
//                     g.ch1_reload_count = g.dma_cndtr1;
//                     g.ch1_half_rem = g.ch1_reload_count / 2;
//                 }
//                 g.ch1_cur_maddr = g.dma_cmar1;
//                 g.ch1_cur_paddr = g.dma_cpar1;
//             }
//             break;
//         }

//         case DMA_CNDTR1_OFF: {
//             // In real HW, NDTR is not supposed to be modified while EN=1.
//             // We'll allow writes when EN=0; if EN=1 ignore to avoid weird behavior.
//             if (!(g.dma_ccr1 & DMA_CCR_EN)) {
//                 reg_write32_masked(&g.dma_cndtr1, addr, value, size);
//                 g.ch1_reload_count = g.dma_cndtr1;
//                 g.ch1_half_rem = g.ch1_reload_count / 2;
//             }
//             break;
//         }

//         case DMA_CPAR1_OFF:
//             if (!(g.dma_ccr1 & DMA_CCR_EN)) {
//                 reg_write32_masked(&g.dma_cpar1, addr, value, size);
//             }
//             break;

//         case DMA_CMAR1_OFF:
//             if (!(g.dma_ccr1 & DMA_CCR_EN)) {
//                 reg_write32_masked(&g.dma_cmar1, addr, value, size);
//             }
//             break;

//         case DMA_ISR_OFF:
//             // ISR is read-only
//             break;

//         default:
//             break;
//     }
// }

// // -------------------- Public API --------------------
// uint64_t adc_with_dma_read(void *opaque, hwaddr addr, unsigned size) {
//     (void)opaque;

//     // Assume absolute addressing as in the trace. Route by range.
//     if (addr >= ADC1_BASE && addr < (ADC1_BASE + 0x400u)) {
//         return adc1_read(addr, size);
//     }
//     if (addr >= DMA1_BASE && addr < (DMA1_BASE + 0x400u)) {
//         return dma1_read(addr, size);
//     }

//     // Unknown access
//     return 0;
// }

// void adc_with_dma_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
//     (void)opaque;

//     if (addr >= ADC1_BASE && addr < (ADC1_BASE + 0x400u)) {
//         adc1_write(addr, value, size);
//         return;
//     }
//     if (addr >= DMA1_BASE && addr < (DMA1_BASE + 0x400u)) {
//         dma1_write(addr, value, size);
//         return;
//     }
//     // Unknown write ignored
// }

// void adc_with_dma_init(ConfigSection* model_info) {
//     (void)model_info;
//     memset(&g, 0, sizeof(g));

//     g.dma1_ch1_irq = 11+16;

//     // Timers:
//     // Periodic ADC sampling/conversion driver (runs but gates on adc_should_run()).
//     // Pick a reasonably fast period so HT+TC can both be set before ISR reads, like hardware.
//     g.adc_periodic_timer = qemu_plugin_timer_new_period_ns(adc_periodic_cb, NULL, 20000 /* 20us */);

//     // One-shot calibration clear timer
//     g.adc_cal_timer = qemu_plugin_timer_new_ns(adc_cal_cb, NULL);

//     // Default ADC sample seed
//     g.sample = 0x0123;

//     // Reset values: keep everything 0; firmware will configure.
//     // Ensure DMA ISR starts clean
//     g.dma_isr = 0;
// }

// Device Model for adc_with_dma (STM32F103 ADC1 + DMA1 Channel1)
// Focus: correct CR2 bit preservation (incl. EXTTRIG bit20) + correct DMA1 ISR/IFCR flag semantics,
//        and realistic ADC->DMA triggering so TCIF1/HTIF1 appear like hardware.
//
// Includes
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <device.h>
#include <boardrunner/vio.h>

#define ADC1_BASE        0x40012400u
#define DMA1_BASE        0x40020000u

// ADC1 register offsets (subset)
#define ADC_SR_OFF       0x00u
#define ADC_CR1_OFF      0x04u
#define ADC_CR2_OFF      0x08u
#define ADC_SMPR1_OFF    0x0Cu
#define ADC_SMPR2_OFF    0x10u
#define ADC_SQR1_OFF     0x2Cu
#define ADC_SQR2_OFF     0x30u
#define ADC_SQR3_OFF     0x34u
#define ADC_DR_OFF       0x4Cu

// ADC_CR2 bits (STM32F1)
#define ADC_CR2_ADON     (1u << 0)
#define ADC_CR2_CONT     (1u << 1)
#define ADC_CR2_CAL      (1u << 2)
#define ADC_CR2_RSTCAL   (1u << 3)
#define ADC_CR2_DMA      (1u << 8)
#define ADC_CR2_EXTTRIG  (1u << 20)   // 0x0010_0000
#define ADC_CR2_SWSTART  (1u << 22)   // 0x0040_0000

// ADC_SR bits (minimal)
#define ADC_SR_EOC       (1u << 1)
#define ADC_SR_STRT      (1u << 4)

// DMA1 register offsets (Channel1 subset)
#define DMA_ISR_OFF      0x00u
#define DMA_IFCR_OFF     0x04u
#define DMA_CCR1_OFF     0x08u
#define DMA_CNDTR1_OFF   0x0Cu
#define DMA_CPAR1_OFF    0x10u
#define DMA_CMAR1_OFF    0x14u

// DMA_CCR bits (STM32F1)
#define DMA_CCR_EN       (1u << 0)
#define DMA_CCR_TCIE     (1u << 1)
#define DMA_CCR_HTIE     (1u << 2)
#define DMA_CCR_TEIE     (1u << 3)
#define DMA_CCR_DIR      (1u << 4)
#define DMA_CCR_CIRC     (1u << 5)
#define DMA_CCR_PINC     (1u << 6)
#define DMA_CCR_MINC     (1u << 7)
#define DMA_CCR_PSIZE_SHIFT 8
#define DMA_CCR_MSIZE_SHIFT 10
#define DMA_CCR_SIZE_MASK   0x3u

// DMA1 ISR bits for Channel1
#define DMA_ISR_GIF1     (1u << 0)
#define DMA_ISR_TCIF1    (1u << 1)
#define DMA_ISR_HTIF1    (1u << 2)
#define DMA_ISR_TEIF1    (1u << 3)

// DMA1 IFCR bits for Channel1
#define DMA_IFCR_CGIF1   (1u << 0) // clears all four for ch1
#define DMA_IFCR_CTCIF1  (1u << 1)
#define DMA_IFCR_CHTIF1  (1u << 2)
#define DMA_IFCR_CTEIF1  (1u << 3)

// In your traces, DMA1_Channel1_IRQHandler corresponds to IRQ line 11.
#define DMA1_CH1_IRQ     11+16

// Pick a small, deterministic conversion period
#define ADC_CONV_LATENCY_NS  5000ull  // 5 us in virtual time (arbitrary but stable)

typedef struct ADCWithDMAState {
    // ADC registers
    uint32_t sr;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t smpr1;
    uint32_t smpr2;
    uint32_t sqr1;
    uint32_t sqr2;
    uint32_t sqr3;
    uint16_t dr;        // 12-bit result stored in 16b container

    // DMA Channel1 registers + minimal internal bookkeeping
    uint32_t dma_isr;   // we only use low 4 bits for ch1
    uint32_t dma_ccr1;
    uint32_t dma_cndtr1;
    uint32_t dma_cpar1;
    uint32_t dma_cmar1;

    uint32_t dma_cndtr1_init;
    uint32_t dma_cmar1_init;
    uint32_t dma_half_point;
    bool     dma_ht_latched;

    // ADC internal
    uint64_t adc_timer;
    bool     adc_busy;
    uint32_t sample_counter;
} ADCWithDMAState;

static void dbg(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

static inline uint32_t le32(const uint8_t b[4]) {
    return ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static inline void st32le(uint8_t b[4], uint32_t v) {
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline uint32_t normalize_off(uint32_t base, hwaddr addr) {
    // Works whether addr is absolute or already an offset.
    if ((uint64_t)addr >= base && (uint64_t)addr < (uint64_t)base + 0x1000ull) {
        return (uint32_t)((uint64_t)addr - (uint64_t)base);
    }
    return (uint32_t)addr;
}

static inline uint32_t mask_write32(uint32_t oldv, uint32_t newv, unsigned size, uint32_t addr_lsb) {
    // Correct partial-width writes: update only the addressed bytes.
    // addr_lsb is (offset & 3).
    uint8_t ob[4], nb[4];
    st32le(ob, oldv);
    st32le(nb, newv);

    uint32_t start = addr_lsb;
    uint32_t end = addr_lsb + size;
    if (end > 4) end = 4;

    for (uint32_t i = 0; i < 4; i++) {
        if (i >= start && i < end) ob[i] = nb[i];
    }
    return le32(ob);
}

static inline unsigned dma_size_bytes(uint32_t ccr, unsigned shift) {
    uint32_t sz = (ccr >> shift) & DMA_CCR_SIZE_MASK;
    switch (sz) {
        case 0: return 1; // 8-bit
        case 1: return 2; // 16-bit
        case 2: return 4; // 32-bit
        default: return 1;
    }
}

static void dma1_ch1_set_flag(ADCWithDMAState *s, uint32_t flag) {
    // Latch flags like hardware; GIF1 is set whenever any event flag is set.
    s->dma_isr |= flag;
    s->dma_isr |= DMA_ISR_GIF1;
}

static void dma1_ch1_maybe_irq(ADCWithDMAState *s, bool tc_event, bool ht_event, bool te_event) {
    // Raise IRQ if enabled for the event (matches STM32F1 behavior closely enough).
    bool fire = false;

    if (tc_event && (s->dma_ccr1 & DMA_CCR_TCIE)) fire = true;
    if (ht_event && (s->dma_ccr1 & DMA_CCR_HTIE)) fire = true;
    if (te_event && (s->dma_ccr1 & DMA_CCR_TEIE)) fire = true;

    if (fire) {
        qemu_plugin_raise_irq(DMA1_CH1_IRQ, false);
    }
}

static void dma1_ch1_reload_cycle(ADCWithDMAState *s) {
    s->dma_cndtr1 = s->dma_cndtr1_init;
    s->dma_cmar1  = s->dma_cmar1_init;
    s->dma_ht_latched = false;
    s->dma_half_point = (s->dma_cndtr1_init > 1) ? (s->dma_cndtr1_init / 2) : 0;
}

static void dma1_ch1_on_enable_edge(ADCWithDMAState *s) {
    // Called when EN transitions 0->1. Hardware conceptually latches NDTR/address for the run.
    s->dma_cndtr1_init = s->dma_cndtr1;
    s->dma_cmar1_init  = s->dma_cmar1;
    s->dma_ht_latched  = false;
    s->dma_half_point  = (s->dma_cndtr1_init > 1) ? (s->dma_cndtr1_init / 2) : 0;
}

static void dma1_ch1_transfer_one_from_adc(ADCWithDMAState *s, uint16_t sample) {
    // Implements a single DMA request serviced immediately (sufficient for ADC-DMA test firmware).
    if (!(s->dma_ccr1 & DMA_CCR_EN)) return;
    if (s->dma_cndtr1 == 0) {
        // If circular, reload; otherwise nothing to do.
        if (s->dma_ccr1 & DMA_CCR_CIRC) dma1_ch1_reload_cycle(s);
        else return;
    }

    const unsigned p_bytes = dma_size_bytes(s->dma_ccr1, DMA_CCR_PSIZE_SHIFT);
    const unsigned m_bytes = dma_size_bytes(s->dma_ccr1, DMA_CCR_MSIZE_SHIFT);

    // Determine direction:
    // DIR=0: read from peripheral (CPAR) -> write to memory (CMAR)  [typical ADC usage]
    // DIR=1: read from memory -> write to peripheral               [we still support, but ADC unlikely uses it]
    const bool dir_mem_to_periph = (s->dma_ccr1 & DMA_CCR_DIR) != 0;

    bool ht_event = false;
    bool tc_event = false;
    bool te_event = false;

    // Very lightweight address sanity: if CPAR looks like ADC_DR, treat sample as source.
    // (We still proceed even if not, because some firmwares misconfigure and still expect IRQs.)
    const uint32_t adc_dr_abs = (uint32_t)(ADC1_BASE + ADC_DR_OFF);

    if (!dir_mem_to_periph) {
        // Peripheral -> Memory
        uint8_t out[4] = {0};

        // Put sample into the peripheral-sized payload, little-endian.
        // If p_bytes==1, low byte; if 2, full 16b; if 4, zero-extended.
        uint32_t pv = (uint32_t)sample;
        st32le(out, pv);

        // Write m_bytes into memory; if m_bytes < p_bytes, truncate; if bigger, zero-extend.
        // We already zero-extended in out[].
        (void)qemu_plugin_write_memory((unsigned long long)s->dma_cmar1, out, (int)m_bytes);

        if (s->dma_ccr1 & DMA_CCR_MINC) s->dma_cmar1 += m_bytes;
        if (s->dma_ccr1 & DMA_CCR_PINC) s->dma_cpar1 += p_bytes;

        // Update remaining count
        s->dma_cndtr1--;

        // Half transfer: when remaining equals half_point (once per cycle).
        if (!s->dma_ht_latched && s->dma_half_point != 0 && s->dma_cndtr1 == s->dma_half_point) {
            s->dma_ht_latched = true;
            dma1_ch1_set_flag(s, DMA_ISR_HTIF1);
            ht_event = true;
        }

        // Transfer complete: when remaining hits 0.
        if (s->dma_cndtr1 == 0) {
            dma1_ch1_set_flag(s, DMA_ISR_TCIF1);
            tc_event = true;

            // In circular mode, hardware continues; we reload AFTER flagging and potential IRQ.
            if (s->dma_ccr1 & DMA_CCR_CIRC) {
                // leave flags set until IFCR clears them
                dma1_ch1_maybe_irq(s, tc_event, ht_event, te_event);
                dma1_ch1_reload_cycle(s);
                return;
            }
        }

        // If CPAR matches ADC_DR, good; if not, still keep IRQ/flags behavior.
        (void)adc_dr_abs;
    } else {
        // Memory -> Peripheral (rare for ADC). We implement as a no-op data write but still consume NDTR for progress.
        uint8_t in[4] = {0};
        (void)qemu_plugin_read_memory((unsigned long long)s->dma_cmar1, in, (int)m_bytes);

        if (s->dma_ccr1 & DMA_CCR_MINC) s->dma_cmar1 += m_bytes;
        if (s->dma_ccr1 & DMA_CCR_PINC) s->dma_cpar1 += p_bytes;

        s->dma_cndtr1--;

        if (!s->dma_ht_latched && s->dma_half_point != 0 && s->dma_cndtr1 == s->dma_half_point) {
            s->dma_ht_latched = true;
            dma1_ch1_set_flag(s, DMA_ISR_HTIF1);
            ht_event = true;
        }
        if (s->dma_cndtr1 == 0) {
            dma1_ch1_set_flag(s, DMA_ISR_TCIF1);
            tc_event = true;
            if (s->dma_ccr1 & DMA_CCR_CIRC) {
                dma1_ch1_maybe_irq(s, tc_event, ht_event, te_event);
                dma1_ch1_reload_cycle(s);
                return;
            }
        }
    }

    // Raise IRQ if enabled for the event(s).
    dma1_ch1_maybe_irq(s, tc_event, ht_event, te_event);
}

static void adc_arm_conversion_timer(ADCWithDMAState *s);

static void adc_conversion_done_cb(void *data) {
    ADCWithDMAState *s = (ADCWithDMAState *)data;
    s->adc_busy = false;

    // Deterministic “analog” value: 12-bit ramp.
    s->sample_counter = (s->sample_counter + 37u) & 0x0FFFu;
    s->dr = (uint16_t)(s->sample_counter & 0x0FFFu);

    // Set status bits
    s->sr |= ADC_SR_EOC;

    // If DMA enabled in ADC CR2, service one DMA request immediately.
    if (s->cr2 & ADC_CR2_DMA) {
        dma1_ch1_transfer_one_from_adc(s, s->dr);
    }

    // Continuous mode: start another conversion.
    if ((s->cr2 & ADC_CR2_ADON) && (s->cr2 & ADC_CR2_CONT)) {
        // Many firmwares leave SWSTART set; we don't require it for continuous once started.
        adc_arm_conversion_timer(s);
    }
}

static void adc_arm_conversion_timer(ADCWithDMAState *s) {
    if (s->adc_busy) return;
    s->adc_busy = true;

    // STRT asserted when conversion starts; EOC will be set on completion.
    s->sr |= ADC_SR_STRT;
    s->sr &= ~ADC_SR_EOC;

    int64_t now = qemu_plugin_get_virtual_timer();
    uint64_t when = (uint64_t)now + ADC_CONV_LATENCY_NS;
    qemu_plugin_timer_alarm(s->adc_timer, when);
}

static void adc_maybe_start_from_cr2(ADCWithDMAState *s, uint32_t old_cr2, uint32_t new_cr2) {
    // Start a conversion if SWSTART is asserted while ADC is ON.
    const bool adon = (new_cr2 & ADC_CR2_ADON) != 0;
    const bool swstart_rise = ((old_cr2 & ADC_CR2_SWSTART) == 0) && ((new_cr2 & ADC_CR2_SWSTART) != 0);

    if (adon && swstart_rise) {
        adc_arm_conversion_timer(s);
    }
}

/* ----------------------- MMIO Read/Write ----------------------- */

uint64_t adc_with_dma_read(void *opaque, hwaddr addr, unsigned size) {
    ADCWithDMAState *s = (ADCWithDMAState *)opaque;

    // Decide whether this hits ADC or DMA based on absolute address range if possible.
    uint32_t off_adc = normalize_off(ADC1_BASE, addr);
    uint32_t off_dma = normalize_off(DMA1_BASE, addr);

    // ADC region
    if ((uint64_t)addr >= ADC1_BASE && (uint64_t)addr < (uint64_t)ADC1_BASE + 0x400ull) {
        switch (off_adc) {
            case ADC_SR_OFF:    return s->sr;
            case ADC_CR1_OFF:   return s->cr1;
            case ADC_CR2_OFF:   return s->cr2;     // IMPORTANT: full 32-bit stored (keeps EXTTRIG bit20)
            case ADC_SMPR1_OFF: return s->smpr1;
            case ADC_SMPR2_OFF: return s->smpr2;
            case ADC_SQR1_OFF:  return s->sqr1;
            case ADC_SQR2_OFF:  return s->sqr2;
            case ADC_SQR3_OFF:  return s->sqr3;
            case ADC_DR_OFF: {
                // Reading DR typically clears EOC in many firmwares’ expectations.
                uint32_t v = (uint32_t)(s->dr & 0xFFFFu);
                s->sr &= ~ADC_SR_EOC;
                return v;
            }
            default:
                return 0;
        }
    }

    // DMA region
    if ((uint64_t)addr >= DMA1_BASE && (uint64_t)addr < (uint64_t)DMA1_BASE + 0x400ull) {
        switch (off_dma) {
            case DMA_ISR_OFF:    return (s->dma_isr & 0x0Fu);
            case DMA_IFCR_OFF:   return 0; // write-only in practice
            case DMA_CCR1_OFF:   return s->dma_ccr1;
            case DMA_CNDTR1_OFF: return s->dma_cndtr1;
            case DMA_CPAR1_OFF:  return s->dma_cpar1;
            case DMA_CMAR1_OFF:  return s->dma_cmar1;
            default:
                return 0;
        }
    }

    // If framework passes offsets (not absolutes), handle by offset heuristics:
    // Prefer ADC offsets first, then DMA offsets.
    switch (off_adc) {
        case ADC_SR_OFF:    return s->sr;
        case ADC_CR1_OFF:   return s->cr1;
        case ADC_CR2_OFF:   return s->cr2;
        case ADC_SMPR1_OFF: return s->smpr1;
        case ADC_SMPR2_OFF: return s->smpr2;
        case ADC_SQR1_OFF:  return s->sqr1;
        case ADC_SQR2_OFF:  return s->sqr2;
        case ADC_SQR3_OFF:  return s->sqr3;
        case ADC_DR_OFF: {
            uint32_t v = (uint32_t)(s->dr & 0xFFFFu);
            s->sr &= ~ADC_SR_EOC;
            return v;
        }
        default:
            break;
    }

    switch (off_dma) {
        case DMA_ISR_OFF:    return (s->dma_isr & 0x0Fu);
        case DMA_CCR1_OFF:   return s->dma_ccr1;
        case DMA_CNDTR1_OFF: return s->dma_cndtr1;
        case DMA_CPAR1_OFF:  return s->dma_cpar1;
        case DMA_CMAR1_OFF:  return s->dma_cmar1;
        default:
            return 0;
    }
}

void adc_with_dma_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    ADCWithDMAState *s = (ADCWithDMAState *)opaque;

    uint32_t off_adc = normalize_off(ADC1_BASE, addr);
    uint32_t off_dma = normalize_off(DMA1_BASE, addr);

    // ADC writes
    if ((uint64_t)addr >= ADC1_BASE && (uint64_t)addr < (uint64_t)ADC1_BASE + 0x400ull) {
        uint32_t old, nw;
        switch (off_adc) {
            case ADC_SR_OFF:
                // Many SR bits are W0C/W1C; for our use, allow firmware to clear EOC/STRT by writing 0 to them.
                old = s->sr;
                nw  = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                // Only allow clearing of the bits we model.
                if (!(nw & ADC_SR_EOC))  s->sr &= ~ADC_SR_EOC;
                if (!(nw & ADC_SR_STRT)) s->sr &= ~ADC_SR_STRT;
                return;

            case ADC_CR1_OFF:
                old = s->cr1;
                s->cr1 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;

            case ADC_CR2_OFF: {
                old = s->cr2;
                // CR2 MUST preserve upper bits on partial writes (this fixes EXTTRIG bit20 drop).
                nw = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));

                // Model self-clearing CAL/RSTCAL roughly (optional, but safe):
                // If firmware sets RSTCAL/CAL, clear them shortly; here we clear immediately.
                if (nw & ADC_CR2_RSTCAL) nw &= ~ADC_CR2_RSTCAL;
                if (nw & ADC_CR2_CAL)    nw &= ~ADC_CR2_CAL;

                s->cr2 = nw;

                // Start conversion if SWSTART rises and ADON is set.
                adc_maybe_start_from_cr2(s, old, s->cr2);
                return;
            }

            case ADC_SMPR1_OFF:
                old = s->smpr1;
                s->smpr1 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;

            case ADC_SMPR2_OFF:
                old = s->smpr2;
                s->smpr2 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;

            case ADC_SQR1_OFF:
                old = s->sqr1;
                s->sqr1 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;

            case ADC_SQR2_OFF:
                old = s->sqr2;
                s->sqr2 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;

            case ADC_SQR3_OFF:
                old = s->sqr3;
                s->sqr3 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;

            case ADC_DR_OFF:
                // Usually read-only; ignore.
                return;

            default:
                return;
        }
    }

    // DMA writes
    if ((uint64_t)addr >= DMA1_BASE && (uint64_t)addr < (uint64_t)DMA1_BASE + 0x400ull) {
        uint32_t old, nw;
        switch (off_dma) {
            case DMA_IFCR_OFF: {
                // Write-1-to-clear semantics for Channel1 flags.
                uint32_t v = (uint32_t)value & 0x0Fu;

                // If CGIF1 set, clear all 4 flags for ch1.
                if (v & DMA_IFCR_CGIF1) {
                    s->dma_isr &= ~(DMA_ISR_GIF1 | DMA_ISR_TCIF1 | DMA_ISR_HTIF1 | DMA_ISR_TEIF1);
                } else {
                    if (v & DMA_IFCR_CTCIF1) s->dma_isr &= ~DMA_ISR_TCIF1;
                    if (v & DMA_IFCR_CHTIF1) s->dma_isr &= ~DMA_ISR_HTIF1;
                    if (v & DMA_IFCR_CTEIF1) s->dma_isr &= ~DMA_ISR_TEIF1;
                    // NOTE: GIF1 stays latched unless CGIF1 is written (matches your trace behavior:
                    // after clearing HTIF1 with 0x4, ISR becomes 0x3, i.e., GIF1+TCIF1).
                }
                return;
            }

            case DMA_CCR1_OFF:
                old = s->dma_ccr1;
                nw  = mask_write32(old, (uint32_t)value, size, (off_dma & 3u));
                s->dma_ccr1 = nw;

                // Detect EN 0->1 edge to latch NDTR/address for this run.
                if (!(old & DMA_CCR_EN) && (nw & DMA_CCR_EN)) {
                    dma1_ch1_on_enable_edge(s);
                }
                // If EN is cleared, we keep registers but stop transfers.
                return;

            case DMA_CNDTR1_OFF:
                old = s->dma_cndtr1;
                s->dma_cndtr1 = mask_write32(old, (uint32_t)value, size, (off_dma & 3u));
                return;

            case DMA_CPAR1_OFF:
                old = s->dma_cpar1;
                s->dma_cpar1 = mask_write32(old, (uint32_t)value, size, (off_dma & 3u));
                return;

            case DMA_CMAR1_OFF:
                old = s->dma_cmar1;
                s->dma_cmar1 = mask_write32(old, (uint32_t)value, size, (off_dma & 3u));
                return;

            case DMA_ISR_OFF:
                // read-only
                return;

            default:
                return;
        }
    }

    // If addr is offset-only (no absolute base), try offset mapping:
    // (This keeps compatibility with frameworks that pass region offsets.)
    if (off_adc <= ADC_DR_OFF) {
        // fall back to ADC logic by re-invoking with absolute-like path
        // simplest: treat as ADC write (already handled above for absolute), so do minimal mapping:
        uint32_t old, nw;
        switch (off_adc) {
            case ADC_CR2_OFF:
                old = s->cr2;
                nw = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                if (nw & ADC_CR2_RSTCAL) nw &= ~ADC_CR2_RSTCAL;
                if (nw & ADC_CR2_CAL)    nw &= ~ADC_CR2_CAL;
                s->cr2 = nw;
                adc_maybe_start_from_cr2(s, old, s->cr2);
                return;
            case ADC_CR1_OFF:
                old = s->cr1;
                s->cr1 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;
            case ADC_SQR1_OFF:
                old = s->sqr1;
                s->sqr1 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;
            case ADC_SQR3_OFF:
                old = s->sqr3;
                s->sqr3 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;
            case ADC_SMPR2_OFF:
                old = s->smpr2;
                s->smpr2 = mask_write32(old, (uint32_t)value, size, (off_adc & 3u));
                return;
            default:
                return;
        }
    }

    if (off_dma <= DMA_CMAR1_OFF) {
        uint32_t old, nw;
        switch (off_dma) {
            case DMA_IFCR_OFF: {
                uint32_t v = (uint32_t)value & 0x0Fu;
                if (v & DMA_IFCR_CGIF1) {
                    s->dma_isr &= ~(DMA_ISR_GIF1 | DMA_ISR_TCIF1 | DMA_ISR_HTIF1 | DMA_ISR_TEIF1);
                } else {
                    if (v & DMA_IFCR_CTCIF1) s->dma_isr &= ~DMA_ISR_TCIF1;
                    if (v & DMA_IFCR_CHTIF1) s->dma_isr &= ~DMA_ISR_HTIF1;
                    if (v & DMA_IFCR_CTEIF1) s->dma_isr &= ~DMA_ISR_TEIF1;
                }
                return;
            }
            case DMA_CCR1_OFF:
                old = s->dma_ccr1;
                nw  = mask_write32(old, (uint32_t)value, size, (off_dma & 3u));
                s->dma_ccr1 = nw;
                if (!(old & DMA_CCR_EN) && (nw & DMA_CCR_EN)) dma1_ch1_on_enable_edge(s);
                return;
            case DMA_CNDTR1_OFF:
                old = s->dma_cndtr1;
                s->dma_cndtr1 = mask_write32(old, (uint32_t)value, size, (off_dma & 3u));
                return;
            case DMA_CPAR1_OFF:
                old = s->dma_cpar1;
                s->dma_cpar1 = mask_write32(old, (uint32_t)value, size, (off_dma & 3u));
                return;
            case DMA_CMAR1_OFF:
                old = s->dma_cmar1;
                s->dma_cmar1 = mask_write32(old, (uint32_t)value, size, (off_dma & 3u));
                return;
            default:
                return;
        }
    }
}

void adc_with_dma_init(ConfigSection* model_info) {
    (void)model_info;

    static ADCWithDMAState s_state;
    memset(&s_state, 0, sizeof(s_state));

    // Reset-like defaults
    s_state.sr  = 0;
    s_state.cr1 = 0;
    s_state.cr2 = 0; // IMPORTANT: store as full 32-bit; do not mask out bit20 etc.
    s_state.dr  = 0;

    // DMA defaults
    s_state.dma_isr   = 0;
    s_state.dma_ccr1  = 0;
    s_state.dma_cndtr1 = 0;
    s_state.dma_cpar1  = 0;
    s_state.dma_cmar1  = 0;

    s_state.dma_cndtr1_init = 0;
    s_state.dma_cmar1_init  = 0;
    s_state.dma_half_point  = 0;
    s_state.dma_ht_latched  = false;

    s_state.sample_counter = 0;
    s_state.adc_busy = false;

    // Create timer for ADC conversion completion
    s_state.adc_timer = qemu_plugin_timer_new_ns(adc_conversion_done_cb, &s_state);

    // If your framework expects you to register the callbacks somewhere, do it outside this file.
    dbg("[adc_with_dma] init done\n");
}
