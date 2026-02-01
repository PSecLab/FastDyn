//contains without fuzzer

// // adc_with_dma.c
// #include <stdint.h>
// #include <stdbool.h>
// #include <string.h>
// #include "utils.h"
// #include <device.h>
// #include <boardrunner/vio.h>

// // ---------------- Base addresses ----------------
// #define ADC1_BASE   0x40012400u
// #define DMA1_BASE   0x40020000u

// // ---------------- ADC register offsets ----------------
// #define ADC_SR_OFF     0x00u
// #define ADC_CR1_OFF    0x04u
// #define ADC_CR2_OFF    0x08u
// #define ADC_SMPR1_OFF  0x0Cu
// #define ADC_SMPR2_OFF  0x10u
// #define ADC_SQR1_OFF   0x2Cu
// #define ADC_SQR2_OFF   0x30u
// #define ADC_SQR3_OFF   0x34u
// #define ADC_DR_OFF     0x4Cu

// // ADC_SR bits
// #define ADC_SR_EOC     (1u << 1)
// #define ADC_SR_STRT    (1u << 4)

// // ADC_CR2 bits
// #define ADC_CR2_ADON    (1u << 0)
// #define ADC_CR2_CONT    (1u << 1)
// #define ADC_CR2_CAL     (1u << 2)
// #define ADC_CR2_RSTCAL  (1u << 3)
// #define ADC_CR2_DMA     (1u << 8)
// #define ADC_CR2_EXTTRIG (1u << 20)
// #define ADC_CR2_SWSTART (1u << 22)

// // ---------------- DMA register offsets ----------------
// #define DMA_ISR_OFF     0x00u
// #define DMA_IFCR_OFF    0x04u

// // Channel 1 regs
// #define DMA_CCR1_OFF    0x08u
// #define DMA_CNDTR1_OFF  0x0Cu
// #define DMA_CPAR1_OFF   0x10u
// #define DMA_CMAR1_OFF   0x14u

// // DMA CCR bits
// #define DMA_CCR_EN      (1u << 0)
// #define DMA_CCR_TCIE    (1u << 1)
// #define DMA_CCR_HTIE    (1u << 2)
// #define DMA_CCR_TEIE    (1u << 3)
// #define DMA_CCR_DIR     (1u << 4)  // 0: P->M, 1: M->P
// #define DMA_CCR_CIRC    (1u << 5)
// #define DMA_CCR_PINC    (1u << 6)
// #define DMA_CCR_MINC    (1u << 7)
// #define DMA_CCR_PSIZE_SHIFT 8
// #define DMA_CCR_MSIZE_SHIFT 10

// // DMA ISR flags for Channel 1
// #define DMA_ISR_GIF1    (1u << 0)
// #define DMA_ISR_TCIF1   (1u << 1)
// #define DMA_ISR_HTIF1   (1u << 2)
// #define DMA_ISR_TEIF1   (1u << 3)

// // DMA IFCR clear bits for Channel 1
// #define DMA_IFCR_CGIF1  (1u << 0)
// #define DMA_IFCR_CTCIF1 (1u << 1)
// #define DMA_IFCR_CHTIF1 (1u << 2)
// #define DMA_IFCR_CTEIF1 (1u << 3)

// typedef struct {
//     // ADC regs
//     uint32_t adc_sr, adc_cr1, adc_cr2;
//     uint32_t adc_smpr1, adc_smpr2;
//     uint32_t adc_sqr1, adc_sqr2, adc_sqr3;
//     uint32_t adc_dr;

//     // ADC internal
//     uint16_t sample12;
//     bool adc_running;
//     uint64_t adc_timer;
//     uint64_t conv_period_ns;

//     // DMA regs (only DMA1 Channel1 + ISR/IFCR)
//     uint32_t dma_isr;
//     uint32_t dma_ccr1;
//     uint32_t dma_cndtr1;
//     uint32_t dma_cpar1;
//     uint32_t dma_cmar1;

//     // latched for circular behavior
//     uint32_t dma_cndtr1_base;
//     uint32_t dma_cmar1_base;

//     int dma_irq_line; // your logs: DMA1 IRQ line 11
// } AdcWithDmaState;

// static AdcWithDmaState g_s;

// // -------- helpers --------
// static inline void le_store32(uint8_t *b, uint32_t v) {
//     b[0] = (uint8_t)(v & 0xFF);
//     b[1] = (uint8_t)((v >> 8) & 0xFF);
//     b[2] = (uint8_t)((v >> 16) & 0xFF);
//     b[3] = (uint8_t)((v >> 24) & 0xFF);
// }
// static inline void le_store16(uint8_t *b, uint16_t v) {
//     b[0] = (uint8_t)(v & 0xFF);
//     b[1] = (uint8_t)((v >> 8) & 0xFF);
// }

// static void recompute_dma_gif1(AdcWithDmaState *s) {
//     // GIF is set when any of HT/TC/TE is set; cleared when none are set.
//     if (s->dma_isr & (DMA_ISR_TCIF1 | DMA_ISR_HTIF1 | DMA_ISR_TEIF1)) {
//         s->dma_isr |= DMA_ISR_GIF1;
//     } else {
//         s->dma_isr &= ~DMA_ISR_GIF1;
//     }
// }

// static uint32_t dma_elem_size(uint32_t ccr, bool mem_side) {
//     uint32_t sh = mem_side ? DMA_CCR_MSIZE_SHIFT : DMA_CCR_PSIZE_SHIFT;
//     uint32_t code = (ccr >> sh) & 0x3; // 00=8, 01=16, 10=32, 11=reserved
//     if (code == 0) return 1;
//     if (code == 1) return 2;
//     if (code == 2) return 4;
//     return 2; // forgiving
// }

// static void dma_raise_if_enabled(AdcWithDmaState *s, bool ht, bool tc) {
//     if (ht && (s->dma_ccr1 & DMA_CCR_HTIE)) qemu_plugin_raise_irq(s->dma_irq_line, false);
//     if (tc && (s->dma_ccr1 & DMA_CCR_TCIE)) qemu_plugin_raise_irq(s->dma_irq_line, false);
// }

// static void dma1_ch1_transfer_from_adc(AdcWithDmaState *s) {
//     // Only P->M for ADC DMA use-case
//     if (!(s->dma_ccr1 & DMA_CCR_EN)) return;
//     if (s->dma_ccr1 & DMA_CCR_DIR) return;
//     if (s->dma_cndtr1 == 0) return;

//     // size selection: pick max(mem, per) so we don't under-write
//     uint32_t msz = dma_elem_size(s->dma_ccr1, true);
//     uint32_t psz = dma_elem_size(s->dma_ccr1, false);
//     uint32_t xsz = (msz > psz) ? msz : psz;
//     if (xsz != 1 && xsz != 2 && xsz != 4) xsz = 2;

//     // write ADC DR into memory
//     uint8_t buf[4] = {0};
//     uint32_t val = s->adc_dr;
//     if (xsz == 1) buf[0] = (uint8_t)(val & 0xFF);
//     else if (xsz == 2) le_store16(buf, (uint16_t)(val & 0xFFFF));
//     else le_store32(buf, val);


//     (void)qemu_plugin_write_memory((unsigned long long)s->dma_cmar1, buf, (int)xsz);

//     // update count/address
//     s->dma_cndtr1--;
//     if (s->dma_ccr1 & DMA_CCR_MINC) s->dma_cmar1 += xsz;

//     bool ht_event = false;
//     bool tc_event = false;

//     // HT at exactly half remaining
//     if (s->dma_cndtr1_base > 1) {
//         uint32_t half = s->dma_cndtr1_base / 2;
//         if (half > 0 && s->dma_cndtr1 == half) {
//             s->dma_isr |= DMA_ISR_HTIF1;
//             ht_event = true;
//         }
//     }

//     // TC at 0 remaining
//     if (s->dma_cndtr1 == 0) {
//         s->dma_isr |= DMA_ISR_TCIF1;
//         tc_event = true;

//         // circular reload
//         if (s->dma_ccr1 & DMA_CCR_CIRC) {
//             s->dma_cndtr1 = s->dma_cndtr1_base;
//             s->dma_cmar1  = s->dma_cmar1_base;
//         }
//     }

//     // IMPORTANT: never clear TC when setting HT (or vice versa).
//     recompute_dma_gif1(s);

//     // interrupt(s)
//     dma_raise_if_enabled(s, ht_event, tc_event);
// }

// static void adc_arm(AdcWithDmaState *s, uint64_t delay_ns) {
//     uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
//     qemu_plugin_timer_alarm(s->adc_timer, now + delay_ns);
// }

// static void adc_conv_cb(void *opaque) {
//     AdcWithDmaState *s = (AdcWithDmaState *)opaque;
//     if (!s->adc_running) return;
//     if (!(s->adc_cr2 & ADC_CR2_ADON)) { s->adc_running = false; return; }

//     // synthetic sample
//     s->sample12 = (uint16_t)((s->sample12 + 0x31u) & 0x0FFFu);
//     s->adc_dr = (uint32_t)s->sample12;

//     // EOC
//     s->adc_sr |= ADC_SR_EOC;

//     // DMA request
//     if (s->adc_cr2 & ADC_CR2_DMA) {
//         dma1_ch1_transfer_from_adc(s);
//     }

//     // continuous scheduling
//     if (s->adc_cr2 & ADC_CR2_CONT) {
//         adc_arm(s, s->conv_period_ns);
//     } else {
//         s->adc_running = false;
//     }
// }

// // ---------------- required API ----------------
// uint64_t adc_with_dma_read(void *opaque, hwaddr addr, unsigned size) {
//     (void)opaque;
//     AdcWithDmaState *s = &g_s;

//     uint32_t a = (uint32_t)addr;
//     uint32_t v = 0;

//     if (a >= ADC1_BASE && a < ADC1_BASE + 0x100u) {
//         uint32_t off = a - ADC1_BASE;
//         switch (off) {
//             case ADC_SR_OFF:    v = s->adc_sr; break;
//             case ADC_CR1_OFF:   v = s->adc_cr1; break;
//             case ADC_CR2_OFF:   v = s->adc_cr2; break;
//             case ADC_SMPR1_OFF: v = s->adc_smpr1; break;
//             case ADC_SMPR2_OFF: v = s->adc_smpr2; break;
//             case ADC_SQR1_OFF:  v = s->adc_sqr1; break;
//             case ADC_SQR2_OFF:  v = s->adc_sqr2; break;
//             case ADC_SQR3_OFF:  v = s->adc_sqr3; break;
//             case ADC_DR_OFF:
//                 v = s->adc_dr;
//                 // DR read clears EOC (safe default)
//                 s->adc_sr &= ~ADC_SR_EOC;
//                 break;
//             default: v = 0; break;
//         }
//     } else if (a >= DMA1_BASE && a < DMA1_BASE + 0x100u) {
//         uint32_t off = a - DMA1_BASE;
//         switch (off) {
//             case DMA_ISR_OFF:    v = s->dma_isr; break;
//             case DMA_CCR1_OFF:   v = s->dma_ccr1; break;
//             case DMA_CNDTR1_OFF: v = s->dma_cndtr1; break;
//             case DMA_CPAR1_OFF:  v = s->dma_cpar1; break;
//             case DMA_CMAR1_OFF:  v = s->dma_cmar1; break;
//             default: v = 0; break;
//         }
//     } else {
//         v = 0;
//     }

//     if (size == 1) return (uint8_t)(v & 0xFF);
//     if (size == 2) return (uint16_t)(v & 0xFFFF);
//     return (uint64_t)v;
// }

// void adc_with_dma_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
//     (void)opaque;
//     AdcWithDmaState *s = &g_s;

//     uint32_t a = (uint32_t)addr;
//     uint32_t v32 = (uint32_t)value;

//     // Sub-word writes as RMW against aligned word (keeps “RMW pattern detected” correct)
//     if (size == 1 || size == 2) {
//         uint32_t aligned = a & ~3u;
//         uint32_t cur = (uint32_t)adc_with_dma_read(NULL, aligned, 4);
//         uint32_t shift = (a & 3u) * 8u;
//         uint32_t mask = (size == 1) ? (0xFFu << shift) : (0xFFFFu << shift);
//         v32 = (cur & ~mask) | ((v32 << shift) & mask);
//         a = aligned;
//     }

//     // ADC
//     if (a >= ADC1_BASE && a < ADC1_BASE + 0x100u) {
//         uint32_t off = a - ADC1_BASE;
//         switch (off) {
//             case ADC_SR_OFF:
//                 // allow W1C-style clearing for robustness
//                 s->adc_sr &= ~v32;
//                 break;

//             case ADC_CR1_OFF:
//                 s->adc_cr1 = v32;
//                 break;

//             case ADC_CR2_OFF: {
//                 // Keep CR2 SW-owned and stable (fix entropy mismatch).
//                 // Only self-clear actual self-clearing bits if set.
//                 s->adc_cr2 = v32;

//                 if (s->adc_cr2 & ADC_CR2_SWSTART) s->adc_cr2 &= ~ADC_CR2_SWSTART;
//                 if (s->adc_cr2 & ADC_CR2_CAL)     s->adc_cr2 &= ~ADC_CR2_CAL;
//                 if (s->adc_cr2 & ADC_CR2_RSTCAL)  s->adc_cr2 &= ~ADC_CR2_RSTCAL;

//                 // Start conversions when ADON is enabled and a “start condition” is present:
//                 // SWSTART, EXTTRIG, or CONT (this is permissive and avoids dead firmware).
//                 bool start = false;
//                 if (s->adc_cr2 & ADC_CR2_ADON) {
//                     if ((v32 & ADC_CR2_SWSTART) || (s->adc_cr2 & ADC_CR2_EXTTRIG) || (s->adc_cr2 & ADC_CR2_CONT)) {
//                         start = true;
//                     }
//                 }

//                 if (start) {
//                     s->adc_running = true;
//                     s->adc_sr |= ADC_SR_STRT;
//                     // schedule first conversion quickly
//                     adc_arm(s, s->conv_period_ns);
//                 } else if (!(s->adc_cr2 & ADC_CR2_ADON)) {
//                     s->adc_running = false;
//                 }
//                 break;
//             }

//             case ADC_SMPR1_OFF: s->adc_smpr1 = v32; break;
//             case ADC_SMPR2_OFF: s->adc_smpr2 = v32; break;
//             case ADC_SQR1_OFF:  s->adc_sqr1  = v32; break;
//             case ADC_SQR2_OFF:  s->adc_sqr2  = v32; break;
//             case ADC_SQR3_OFF:  s->adc_sqr3  = v32; break;

//             default:
//                 break;
//         }
//         return;
//     }

//     // DMA1
//     if (a >= DMA1_BASE && a < DMA1_BASE + 0x100u) {
//         uint32_t off = a - DMA1_BASE;
//         switch (off) {
//             case DMA_IFCR_OFF:
//                 // W1C: clear only what is requested.
//                 if (v32 & DMA_IFCR_CGIF1)  s->dma_isr &= ~(DMA_ISR_GIF1 | DMA_ISR_TCIF1 | DMA_ISR_HTIF1 | DMA_ISR_TEIF1);
//                 if (v32 & DMA_IFCR_CTCIF1) s->dma_isr &= ~DMA_ISR_TCIF1;
//                 if (v32 & DMA_IFCR_CHTIF1) s->dma_isr &= ~DMA_ISR_HTIF1;
//                 if (v32 & DMA_IFCR_CTEIF1) s->dma_isr &= ~DMA_ISR_TEIF1;
//                 recompute_dma_gif1(s);
//                 break;

//             case DMA_CCR1_OFF: {
//                 uint32_t prev = s->dma_ccr1;

//                 // Preserve written bits; mask only high reserved (CCR is 16-ish meaningful bits on F1)
//                 s->dma_ccr1 = v32 & 0x7FFFu;

//                 bool prev_en = (prev & DMA_CCR_EN) != 0;
//                 bool new_en  = (s->dma_ccr1 & DMA_CCR_EN) != 0;

//                 if (!prev_en && new_en) {
//                     // latch base for HT/TC and circular reload
//                     s->dma_cndtr1_base = s->dma_cndtr1;
//                     s->dma_cmar1_base  = s->dma_cmar1;

//                     // safety: avoid “never fires” if firmware enables before programming count
//                     if (s->dma_cndtr1_base == 0) {
//                         s->dma_cndtr1_base = 2;
//                         s->dma_cndtr1 = 2;
//                     }
//                 }
//                 break;
//             }

//             case DMA_CNDTR1_OFF:
//                 s->dma_cndtr1 = v32 & 0xFFFFu;
//                 if (s->dma_ccr1 & DMA_CCR_EN) {
//                     s->dma_cndtr1_base = s->dma_cndtr1 ? s->dma_cndtr1 : 2;
//                     s->dma_cndtr1 = s->dma_cndtr1_base;
//                 }
//                 break;

//             case DMA_CPAR1_OFF:
//                 s->dma_cpar1 = v32;
//                 break;

//             case DMA_CMAR1_OFF:
//                 s->dma_cmar1 = v32;
//                 if (s->dma_ccr1 & DMA_CCR_EN) s->dma_cmar1_base = s->dma_cmar1;
//                 break;

//             default:
//                 break;
//         }
//         return;
//     }
// }

// void adc_with_dma_init(ConfigSection* model_info) {
//     (void)model_info;
//     memset(&g_s, 0, sizeof(g_s));

//     g_s.dma_irq_line = 11+16;

//     // Make conversions fast enough that HT+TC can both be pending when the ISR is read (matches HW seeing 0x7).
//     // This is still “high-fidelity”: it’s just a fast continuous ADC.
//     g_s.conv_period_ns = 500; // 2 us

//     g_s.sample12 = 0x120;
//     g_s.adc_running = false;

//     g_s.adc_timer = qemu_plugin_timer_new_ns(adc_conv_cb, &g_s);

//     // Optional:
//     // dev_debug("adc_with_dma_init: initialized ADC1 + DMA1_CH1 model\n");
// }


/*
 * STM32F7x9 Ethernet MAC/MMC/DMA Device Model — Milestone C (DEBUG, polling-only)
 *
 * Goals:
 *  - NO IRQs (your firmware polls via ethernetif_input / HAL_ETH_ReadData)
 *  - Ghost PHY that reliably reports LINK UP (incl. reg31 "special status")
 *  - Polling timer drains TAP and delivers frames into RX descriptors
 *  - TX scans descriptors and sends frames to TAP
 *  - Verbose logging of:
 *      - MACCR / DMAOMR gating bits
 *      - PHY MII reads/writes (addr/reg/value)
 *      - RX/TX desc OWN + key words (optional dump)
 *
 * Env knobs:
 *   ETH_TAP=tap0
 *   ETH_LOG_LEVEL=0|1|2    (default 2)
 *   ETH_POLL_NS=5000000    (default 5ms)
 *   ETH_RX_POOL_ENABLE=0|1 (default 1; if firmware leaves DESC2=0)
 *   ETH_IGNORE_ENABLE=0|1  (default 0; if 1, ignore RE/TE and SR/ST gating)
 *   ETH_PROMISC=0|1        (default 1; if 0, drop multicast noise; keep broadcast)
 *
 * Notes:
 *  - Descriptor stride is 0x28 (40 bytes) in your traces, so we treat each desc as 10 u32 words.
 *  - RX: when delivering a frame, we clear OWN, set FS/LS, and set FL including CRC (+4)
 *  - TX: when sending, we clear OWN and set TI in DMASR.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <device.h>
#include <boardrunner/vio.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

// ---- MMIO base addresses ----
#define ETH_MAC_BASE   ((hwaddr)0x40028000ull)
#define ETH_MMC_BASE   ((hwaddr)0x40028100ull)
#define ETH_DMA_BASE   ((hwaddr)0x40029000ull)

// ---- MAC offsets (subset) ----
#define MACCR_OFF      0x0000u
#define MACMIIAR_OFF   0x0010u
#define MACMIIDR_OFF   0x0014u
#define MACIMR_OFF     0x003Cu
#define MACA0HR_OFF    0x0040u
#define MACA0LR_OFF    0x0044u

// MACCR bits
#define MACCR_RE_BIT   (1u << 2)
#define MACCR_TE_BIT   (1u << 3)

// ---- MMC offsets (subset) ----
#define MMCRIMR_OFF    0x000Cu
#define MMCTIMR_OFF    0x0010u

// ---- DMA offsets ----
#define DMABMR_OFF     0x0000u
#define DMATPDR_OFF    0x0004u
#define DMARPDR_OFF    0x0008u
#define DMARDLAR_OFF   0x000Cu
#define DMATDLAR_OFF   0x0010u
#define DMASR_OFF      0x0014u
#define DMAOMR_OFF     0x0018u

// ---- DMASR bits (subset) ----
#define DMASR_TI               (1u << 0)
#define DMASR_RI               (1u << 6)
#define DMASR_RU               (1u << 7)
#define DMASR_AIS              (1u << 15)
#define DMASR_NIS              (1u << 16)
#define DMASR_W1C_MASK         (0x0001FFFFu)

// ---- DMAOMR bits (Synopsys) ----
#define DMAOMR_SR              (1u << 1)     // Start/Stop Receive
#define DMAOMR_ST              (1u << 13)    // Start/Stop Transmission

// ---- DMABMR ----
#define DMABMR_SWR             (1u << 0)

// ---- Descriptor ----
#define ETH_DESC_STRIDE_BYTES  0x28u
typedef struct EthDesc40 { uint32_t w[10]; } EthDesc40;

// DESC0 bits
#define DESC_OWN_BIT      (1u << 31)
#define RXDESC_LS_BIT     (1u << 8)
#define RXDESC_FS_BIT     (1u << 9)
#define RXDESC_FL_SHIFT   16
#define RXDESC_FL_MASK    (0x3FFFu << RXDESC_FL_SHIFT)

// DESC1 bits
#define RXDESC_RBS1_MASK  (0x1FFFu)
#define RXDESC_RER_BIT    (1u << 15)
#define TXDESC_TER_BIT    (1u << 21)
#define TXDESC_TBS1_MASK  (0x1FFFu)
#define TXDESC_TCH_BIT (1u << 20)   // in DESC0
#define TXDESC_FS_BIT (1u << 28)
#define TXDESC_LS_BIT (1u << 29)

// ---- Ethernet ----
#define ETH_MAX_FRAME          1600

#define RXDESC_RCH_BIT    (1u << 14)   // Receive Chained
#define TXDESC_TCH_BIT    (1u << 20)   // Transmit Chained
#define TXDESC_TBS2_MASK  (0x1FFFu << 16)

#define TXDESC_CIC_MASK   (3u << 22)
#define TXDESC_CIC_SHIFT  22
#define TXDESC_CIC_BYPASS 0u
#define TXDESC_CIC_IPHDR  1u
#define TXDESC_CIC_FULL   2u

// ---- Logging ----
static int g_log_level = 2; // 0=quiet, 1=milestone, 2=verbose
#define LOG1(...) do { if (g_log_level >= 1) { printf(__VA_ARGS__); fflush(stdout); } } while (0)
#define LOG2(...) do { if (g_log_level >= 2) { printf(__VA_ARGS__); fflush(stdout); } } while (0)

// ---- State ----
typedef struct EthernetState {
  // MAC
  uint32_t MACCR, MACMIIAR, MACMIIDR, MACIMR, MACA0HR, MACA0LR;
  uint16_t phy_regs[32];

  // MMC
  uint32_t MMCRIMR, MMCTIMR;

  // DMA
  uint32_t DMABMR, DMATPDR, DMARPDR, DMARDLAR, DMATDLAR, DMASR, DMAOMR;

  // ring pointers
  hwaddr tx_cur, rx_cur;
  bool have_tx_base, have_rx_base;

  // tap + poll
  int tap_fd;
  uint64_t poll_timer_id;
  uint32_t poll_ticks;

  // RX pool
  bool   rx_pool_enable;
  bool   rx_pool_inited;
  hwaddr rx_pool_base;
  hwaddr rx_pool_next;
  uint32_t rx_pool_bufsz;

  // debug modes
  bool ignore_enable;  // ignore RE/TE and SR/ST gating
  bool promisc;        // accept all (except optional multicast drop)

  // counters
  uint32_t seen_rx_frames;
  uint32_t sent_tx_frames;
  uint32_t tap_pkts;
  uint32_t tap_bytes;

  bool dumped_rings_once;

 // fuzz knobs
  bool fuzz_enable;
  uint32_t fuzz_every_n;     // mutate 1 out of N matching packets
  uint32_t fuzz_mut_max;     // max bytes to mutate in payload slice
  uint32_t fuzz_hits;        // counter for gating
  uint32_t fuzz_seed;

  // in EthernetState
uint32_t rx_http_seen;          // counts only http-ish tcp:80 payload packets

// injections (0 disables)
uint32_t inj_frag_every_n;      // e.g., 200  -> 1/200 http packets become 2 IPv4 fragments
uint32_t inj_split_every_n;     // e.g., 50   -> 1/50  http packets become 2 TCP segments
uint32_t inj_reorder_every_n;   // e.g., 300  -> 1/300 split injections are delivered out-of-order
uint32_t inj_dup_every_n;       // e.g., 80   -> 1/80  deliver a duplicate after normal deliver
uint32_t inj_badhdr_every_n;    // e.g., 500  -> 1/500 corrupt a header field (very low-rate)



} EthernetState;

static EthernetState g_eth;

// ---- helpers ----

static inline uint32_t rd_be32_u(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void wr_be32_u(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)(v & 0xFF);
}

static inline uint16_t rd_be16_u2(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint8_t map_token_char(uint8_t x) {
  static const char tok[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "-_.%?&=+";
  return (uint8_t)tok[x % (sizeof(tok) - 1)];
}

typedef struct TcpMeta {
  uint32_t sip, dip;
  uint16_t sport, dport;
  uint32_t seq;

  // NEW: enough to fragment/split correctly
  uint16_t ip_tot;     // from IP header
  uint8_t  l3_off;     // 14 or 18 (VLAN)
  uint8_t  ip_hlen;    // bytes
  uint8_t  tcp_hlen;   // bytes
} TcpMeta;

static uint32_t xorshift32(uint32_t *s) {
  uint32_t x = *s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *s = x;
  return x;
}

static uint32_t fnv1a32(const void *data, size_t n) {
  const uint8_t *p = (const uint8_t*)data;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}

typedef struct FlowKey {
  uint32_t sip, dip;
  uint16_t sport, dport;
  uint32_t seq;
} FlowKey;

static uint32_t seed_for_seg(const FlowKey *k, uint32_t global_seed) {
  uint32_t h = fnv1a32(k, sizeof(*k));
  return h ^ global_seed ^ 0x9e3779b9u;
}

static void fuzz_fill_bytes(uint8_t *dst, size_t len, const FlowKey *k)
{
  uint32_t s = seed_for_seg(k, g_eth.fuzz_seed);

  // TODAY: PRNG bytes
  for (size_t i = 0; i < len; i++) {
    dst[i] = (uint8_t)(xorshift32(&s) & 0xFF);
  }

  // LATER (LibAFL): replace the loop with get_data(dst, len)
  // BUT keep the seed_for_seg concept or add a cache so retransmits are stable.
}



static inline bool looks_like_sram_addr(uint32_t v) {
  // Allow SRAM (0x20000000) AND Flash (0x08000000)
  if ((v >= 0x20000000u) && (v < 0x30000000u)) return true;
  if ((v >= 0x08000000u) && (v < 0x08200000u)) return true; // 2MB Flash window
  return false;
}
static inline bool looks_like_sram_wordptr(uint32_t v) {
  return looks_like_sram_addr(v) && ((v & 3u) == 0);
}

static inline hwaddr align_up(hwaddr v, hwaddr a) {
  return (a ? (hwaddr)((v + (a - 1)) & ~(a - 1)) : v);
}
static int mem_read(hwaddr addr, void *out, int len) {
  return qemu_plugin_read_memory((unsigned long long)addr, (uint8_t*)out, len);
}
static int mem_write(hwaddr addr, const void *in, int len) {
  return qemu_plugin_write_memory((unsigned long long)addr, (uint8_t*)in, len);
}

static uint32_t merge_subwrite_u32(uint32_t oldv, uint64_t value, unsigned size, unsigned byte_off)
{
  uint32_t mask;
  if (size == 1) mask = 0xFFu << (byte_off * 8);
  else if (size == 2) mask = 0xFFFFu << (byte_off * 8);
  else { mask = 0xFFFFFFFFu; byte_off = 0; }

  uint32_t v32 = (uint32_t)value;
  uint32_t shifted = (byte_off ? (v32 << (byte_off * 8)) : v32);
  return (oldv & ~mask) | (shifted & mask);
}
static uint64_t subread_u32(uint32_t v, unsigned size, unsigned byte_off)
{
  if (size == 1) return (v >> (byte_off * 8)) & 0xFFu;
  if (size == 2) return (v >> (byte_off * 8)) & 0xFFFFu;
  return (uint64_t)v;
}

static bool desc_read(hwaddr desc_addr, EthDesc40 *d)
{
  memset(d, 0, sizeof(*d));
  return mem_read(desc_addr, d, (int)sizeof(*d)) == 0;
}
static bool desc_write(hwaddr desc_addr, const EthDesc40 *d)
{
  return mem_write(desc_addr, d, (int)sizeof(*d)) == 0;
}

static inline bool mac_rx_enabled(void) { return (g_eth.MACCR & MACCR_RE_BIT) != 0; }
static inline bool mac_tx_enabled(void) { return (g_eth.MACCR & MACCR_TE_BIT) != 0; }
static inline bool dma_rx_started(void) { return (g_eth.DMAOMR & DMAOMR_SR) != 0; }
static inline bool dma_tx_started(void) { return (g_eth.DMAOMR & DMAOMR_ST) != 0; }

static uint32_t rx_bufcap_from_des1(uint32_t des1) { return des1 & RXDESC_RBS1_MASK; }
static uint32_t tx_len_from_des1(uint32_t des1)    { return des1 & TXDESC_TBS1_MASK; }

static hwaddr next_desc_addr_rx(const EthDesc40 *d, hwaddr cur)
{
  uint32_t des1 = d->w[1];
  uint32_t d3   = d->w[3];

  if ((des1 & RXDESC_RCH_BIT) && looks_like_sram_wordptr(d3)) return (hwaddr)d3;

  if (des1 & RXDESC_RER_BIT) {
    if (looks_like_sram_wordptr(g_eth.DMARDLAR)) return (hwaddr)g_eth.DMARDLAR;
  }
  return (hwaddr)(cur + (hwaddr)ETH_DESC_STRIDE_BYTES);
}

static hwaddr next_desc_addr_tx(const EthDesc40 *d, hwaddr cur)
{
  uint32_t des0 = d->w[0];
  uint32_t des1 = d->w[1];
  uint32_t d3   = d->w[3];

  if ((des0 & TXDESC_TCH_BIT) && looks_like_sram_wordptr(d3)) return (hwaddr)d3;

  if (des1 & TXDESC_TER_BIT) {
    if (looks_like_sram_wordptr(g_eth.DMATDLAR)) return (hwaddr)g_eth.DMATDLAR;
  }
  return (hwaddr)(cur + (hwaddr)ETH_DESC_STRIDE_BYTES);
}


static void dump_first_descs_once(void)
{
  if (g_eth.dumped_rings_once) return;
  if (!g_eth.have_rx_base || !g_eth.have_tx_base) return;
  if (!looks_like_sram_wordptr(g_eth.DMARDLAR) || !looks_like_sram_wordptr(g_eth.DMATDLAR)) return;

  g_eth.dumped_rings_once = true;

  LOG1("[eth] --- ring dump (first 4 RX/TX desc) ---\n");
  for (int i = 0; i < 4; i++) {
    hwaddr a = (hwaddr)g_eth.DMARDLAR + (hwaddr)(i * ETH_DESC_STRIDE_BYTES);
    EthDesc40 d;
    if (desc_read(a, &d)) {
      LOG1("[eth] RX[%d] @0x%08x: D0=%08x D1=%08x D2=%08x D3=%08x\n",
           i, (unsigned)a, d.w[0], d.w[1], d.w[2], d.w[3]);
    }
  }
  for (int i = 0; i < 4; i++) {
    hwaddr a = (hwaddr)g_eth.DMATDLAR + (hwaddr)(i * ETH_DESC_STRIDE_BYTES);
    EthDesc40 d;
    if (desc_read(a, &d)) {
      LOG1("[eth] TX[%d] @0x%08x: D0=%08x D1=%08x D2=%08x D3=%08x\n",
           i, (unsigned)a, d.w[0], d.w[1], d.w[2], d.w[3]);
    }
  }
}

static uint16_t csum16(const uint8_t *buf, uint32_t len)
{
  uint32_t sum = 0;
  while (len > 1) {
    sum += ((uint32_t)buf[0] << 8) | buf[1];
    buf += 2; len -= 2;
  }
  if (len) sum += ((uint32_t)buf[0] << 8);

  while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
  return (uint16_t)~sum;
}

static void tx_apply_hw_csum(uint32_t first_des0, uint8_t *eth, uint32_t eth_len)
{
  uint32_t cic = (first_des0 & TXDESC_CIC_MASK) >> TXDESC_CIC_SHIFT;
  if (cic == TXDESC_CIC_BYPASS) return;

  if (eth_len < 14) return;
  uint16_t etype = (uint16_t)((eth[12] << 8) | eth[13]);
  if (etype != 0x0800) return; // IPv4 only

  if (eth_len < 14 + 20) return;
  uint8_t *ip = eth + 14;
  uint8_t ver_ihl = ip[0];
  if ((ver_ihl >> 4) != 4) return;
  uint32_t ihl = (ver_ihl & 0x0F) * 4;
  if (ihl < 20 || eth_len < 14 + ihl) return;

  uint16_t ip_tot = (uint16_t)((ip[2] << 8) | ip[3]);
  if (ip_tot < ihl) return;
  if (eth_len < 14 + ip_tot) return;

  // Always do IPv4 header checksum if requested
  if (cic == TXDESC_CIC_IPHDR || cic == TXDESC_CIC_FULL) {
    ip[10] = 0; ip[11] = 0;
    uint16_t ipcs = csum16(ip, ihl);
    ip[10] = (uint8_t)(ipcs >> 8);
    ip[11] = (uint8_t)(ipcs & 0xFF);
  }

  if (cic != TXDESC_CIC_FULL) return;

  uint8_t proto = ip[9];
  uint8_t *l4 = ip + ihl;
  uint32_t l4_len = (uint32_t)ip_tot - ihl;

  if (l4_len < 4) return;

  // TCP
  if (proto == 6 && l4_len >= 20) {
    l4[16] = 0; l4[17] = 0;
    uint32_t sum = 0;
    // pseudo-header
    sum += ((uint32_t)ip[12] << 8) | ip[13];
    sum += ((uint32_t)ip[14] << 8) | ip[15];
    sum += ((uint32_t)ip[16] << 8) | ip[17];
    sum += ((uint32_t)ip[18] << 8) | ip[19];
    sum += proto;
    sum += l4_len;
    // payload
    const uint8_t *p = l4;
    uint32_t n = l4_len;
    while (n > 1) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) sum += ((uint32_t)p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    uint16_t cs = (uint16_t)~sum;
    l4[16] = (uint8_t)(cs >> 8);
    l4[17] = (uint8_t)(cs & 0xFF);
    return;
  }

  // UDP
  if (proto == 17 && l4_len >= 8) {
    l4[6] = 0; l4[7] = 0;
    uint32_t sum = 0;
    sum += ((uint32_t)ip[12] << 8) | ip[13];
    sum += ((uint32_t)ip[14] << 8) | ip[15];
    sum += ((uint32_t)ip[16] << 8) | ip[17];
    sum += ((uint32_t)ip[18] << 8) | ip[19];
    sum += proto;
    sum += l4_len;

    const uint8_t *p = l4;
    uint32_t n = l4_len;
    while (n > 1) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) sum += ((uint32_t)p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    uint16_t cs = (uint16_t)~sum;
    if (cs == 0) cs = 0xFFFF; // UDP checksum: 0 means “not used”
    l4[6] = (uint8_t)(cs >> 8);
    l4[7] = (uint8_t)(cs & 0xFF);
    return;
  }

  // ICMP
  if (proto == 1 && l4_len >= 4) {
    l4[2] = 0; l4[3] = 0;
    uint16_t cs = csum16(l4, l4_len);
    l4[2] = (uint8_t)(cs >> 8);
    l4[3] = (uint8_t)(cs & 0xFF);
    return;
  }
}

static inline uint16_t rd_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static inline void wr_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

/* 16-bit one's complement sum */
static uint32_t csum_accum(const uint8_t *buf, size_t len) {
    uint32_t sum = 0;
    size_t i = 0;
    while (i + 1 < len) {
        sum += (uint16_t)((buf[i] << 8) | buf[i + 1]);
        i += 2;
    }
    if (i < len) {
        sum += (uint16_t)(buf[i] << 8);
    }
    return sum;
}
static uint16_t csum_finalize(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* Compute IPv4 header checksum (writes into header) */
static void ipv4_fix_hdr_checksum(uint8_t *ip, size_t ip_len) {
    if (ip_len < 20) return;
    uint8_t ihl = (uint8_t)(ip[0] & 0x0F);
    size_t hdr_len = (size_t)ihl * 4;
    if (ihl < 5 || hdr_len > ip_len) return;

    wr_be16(&ip[10], 0); // checksum field = 0
    uint32_t sum = csum_accum(ip, hdr_len);
    wr_be16(&ip[10], csum_finalize(sum));
}

/* Compute TCP/UDP checksum (writes into L4 header) */
static void ipv4_fix_l4_checksum(uint8_t *ip, size_t ip_len) {
    if (ip_len < 20) return;
    uint8_t ihl = (uint8_t)(ip[0] & 0x0F);
    size_t ip_hdr_len = (size_t)ihl * 4;
    if (ihl < 5 || ip_hdr_len > ip_len) return;

    uint16_t total_len = rd_be16(&ip[2]);
    if (total_len < ip_hdr_len || total_len > ip_len) return;

    uint8_t proto = ip[9];
    uint8_t *l4 = ip + ip_hdr_len;
    size_t l4_len = (size_t)total_len - ip_hdr_len;

    int is_tcp = (proto == 6);
    int is_udp = (proto == 17);
    if (!is_tcp && !is_udp) return;
    if (l4_len < (is_tcp ? 20 : 8)) return;

    size_t cksum_off = is_tcp ? 16 : 6;
    wr_be16(&l4[cksum_off], 0);

    /* pseudo-header calculation */
    uint32_t sum = 0;
    sum += csum_accum(&ip[12], 8);       // Source & Dest IP
    sum += (uint32_t)proto;              // Protocol (added ONCE)
    sum += (uint32_t)(l4_len & 0xFFFFu); // TCP/UDP Length

    /* add TCP/UDP header+payload */
    sum += csum_accum(l4, l4_len);

    uint16_t csum = csum_finalize(sum);
    if (is_udp && csum == 0) csum = 0xFFFF;

    wr_be16(&l4[cksum_off], csum);
}

/* Call this on the *full Ethernet frame* you are about to write to TAP */
static void eth_fixup_tx_checksums(uint8_t *frame, size_t frame_len) {
    if (frame_len < 14) return;

    size_t l3_off = 14;
    uint16_t et = rd_be16(&frame[12]);

    /* VLAN tag */
    if (et == 0x8100) {
        if (frame_len < 18) return;
        et = rd_be16(&frame[16]);
        l3_off = 18;
    }

    if (et != 0x0800) return; /* IPv4 only */
    if (frame_len <= l3_off) return;

    uint8_t *ip = frame + l3_off;
    size_t ip_len = frame_len - l3_off;

    /* Only IPv4 version */
    if ((ip[0] >> 4) != 4) return;

    ipv4_fix_hdr_checksum(ip, ip_len);
    ipv4_fix_l4_checksum(ip, ip_len);
}

static inline uint8_t rd_u8(const uint8_t *p) { return p[0]; }
static inline uint16_t rd_be16_u(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static bool parse_ipv4_tcp_payload(uint8_t *frame, size_t frame_len,
                                  size_t *tcp_pay_off, size_t *tcp_pay_len,
                                  TcpMeta *m)
{
  if (frame_len < 14) return false;

  size_t l3_off = 14;
  uint16_t et = rd_be16_u(&frame[12]);

  if (et == 0x8100) {
    if (frame_len < 18) return false;
    et = rd_be16_u(&frame[16]);
    l3_off = 18;
  }

  if (et != 0x0800) return false;
  if (frame_len < l3_off + 20) return false;

  uint8_t *ip = frame + l3_off;
  if ((ip[0] >> 4) != 4) return false;

  uint8_t ihl = (uint8_t)(ip[0] & 0x0F);
  size_t ip_hlen = (size_t)ihl * 4;
  if (ihl < 5) return false;
  if (frame_len < l3_off + ip_hlen) return false;

  uint16_t ip_tot = rd_be16_u(&ip[2]);
  if (ip_tot < ip_hlen) return false;
  if (frame_len < l3_off + ip_tot) return false;

  if (ip[9] != 6) return false; // TCP

  size_t tcp_off_in_frame = l3_off + ip_hlen;
  if (ip_tot < ip_hlen + 20) return false;
  if (frame_len < tcp_off_in_frame + 20) return false;

  uint8_t *tcp = ip + ip_hlen;


  uint8_t doff = (uint8_t)(tcp[12] >> 4);
  size_t tcp_hlen = (size_t)doff * 4;
  if (doff < 5) return false;
  if (ip_tot < ip_hlen + tcp_hlen) return false;
  if (frame_len < tcp_off_in_frame + tcp_hlen) return false;

  size_t pay_off = tcp_off_in_frame + tcp_hlen;
  size_t pay_len = (size_t)ip_tot - ip_hlen - tcp_hlen;

  // Now it's safe to read TCP fields

    if (m) {
    m->sip   = rd_be32_u(&ip[12]);
    m->dip   = rd_be32_u(&ip[16]);
    m->sport = rd_be16_u(&tcp[0]);
    m->dport = rd_be16_u(&tcp[2]);
    m->seq   = rd_be32_u(&tcp[4]);

    m->l3_off = (uint8_t)l3_off;
    m->ip_hlen = (uint8_t)ip_hlen;
    m->tcp_hlen = (uint8_t)tcp_hlen;
    m->ip_tot = ip_tot;
    }


  if (frame_len < pay_off + pay_len) return false;

  *tcp_pay_off = pay_off;
  *tcp_pay_len = pay_len;
  return true;
}

static bool looks_like_http_req(const uint8_t *p, size_t n)
{
  if (n < 8) return false;
  // super cheap “HTTP-ish” gate
  if (!memcmp(p, "GET ", 4)) return true;
  if (!memcmp(p, "POST", 4)) return true;
  if (!memcmp(p, "HEAD", 4)) return true;
  if (!memcmp(p, "PUT ", 4)) return true;
  if (!memcmp(p, "OPTI", 4)) return true; // OPTIONS
  return false;
}

static void mutate_http_inplace(uint8_t *tcp_payload, size_t pay_len, const FlowKey *k)
{
  if (pay_len < 16) return;
  if (memcmp(tcp_payload, "GET ", 4) != 0) return;

  size_t i = 4;
  if (i >= pay_len) return;
  if (tcp_payload[i] != '/') return;

  size_t path_start = i + 1;
  size_t sp = 0;
  for (size_t t = i; t < pay_len; t++) {
    if (tcp_payload[t] == ' ') { sp = t; break; }
    if (tcp_payload[t] == '\r' || tcp_payload[t] == '\n') return;
  }
  if (!sp || sp <= path_start) return;

  size_t path_len = sp - path_start;
  size_t m = path_len;
  if (g_eth.fuzz_mut_max && m > g_eth.fuzz_mut_max) m = g_eth.fuzz_mut_max;

  uint8_t tmp[256];
  if (m > sizeof(tmp)) m = sizeof(tmp);
  fuzz_fill_bytes(tmp, m, k);

  for (size_t j = 0; j < m; j++) {
    uint8_t c = tcp_payload[path_start + j];
    if (c == '/' || c == ' ' || c == '\r' || c == '\n') continue;

    // map to visible token-ish chars
    uint8_t out = map_token_char(tmp[j]);
    tcp_payload[path_start + j] = out;
  }
}

static void maybe_fuzz_rx_frame(uint8_t *frame, size_t frame_len)
{
  if (!g_eth.fuzz_enable) return;

size_t pay_off = 0, pay_len = 0;
TcpMeta tm;
if (!parse_ipv4_tcp_payload(frame, frame_len, &pay_off, &pay_len, &tm)) return;
if (tm.dport != 80) return;
if (pay_len == 0) return;

FlowKey k = { tm.sip, tm.dip, tm.sport, tm.dport, tm.seq };

  uint8_t *pay = frame + pay_off;
  if (!looks_like_http_req(pay, pay_len)) return;

  // Gate: mutate 1/N packets
  g_eth.fuzz_hits++;
  if (g_eth.fuzz_every_n > 1) {
    if ((g_eth.fuzz_hits % g_eth.fuzz_every_n) != 0) return;
  }

  // mutate in-place (length preserved)
mutate_http_inplace(pay, pay_len, &k);

  // Fix checksums (IPv4 + TCP) after payload change
  // Reuse your existing helper; it reads total_len and recomputes.
  // We must pass it the IP header pointer and available bytes from L3.
  size_t l3_off = 14;
  uint16_t et = rd_be16_u(&frame[12]);
  if (et == 0x8100) l3_off = 18;

  if (frame_len > l3_off) {
    uint8_t *ip = frame + l3_off;
    size_t ip_avail = frame_len - l3_off;
    ipv4_fix_hdr_checksum(ip, ip_avail);
    ipv4_fix_l4_checksum(ip, ip_avail);
  }

  LOG1("[eth] FUZZ: mutated HTTP payload (dst=80) frame_len=%u\n", (unsigned)frame_len);
}

// If firmware leaves DESC2=0, allocate RX buffers in guest SRAM (simple pool).
static void rx_fixup_ring_buffers(void)
{
  if (!g_eth.rx_pool_enable) return;
  if (!g_eth.have_rx_base || !looks_like_sram_wordptr(g_eth.DMARDLAR)) return;

  EthDesc40 first;
  if (!desc_read((hwaddr)g_eth.DMARDLAR, &first)) return;

  uint32_t cap = rx_bufcap_from_des1(first.w[1]);
  if (cap == 0 || cap > 2048) cap = 1528;
  g_eth.rx_pool_bufsz = cap;

  if (!g_eth.rx_pool_inited) {
    hwaddr hi = (g_eth.DMATDLAR > g_eth.DMARDLAR) ? (hwaddr)g_eth.DMATDLAR : (hwaddr)g_eth.DMARDLAR;
    g_eth.rx_pool_base = align_up(hi + 0x0400u, 32u);
    g_eth.rx_pool_next = g_eth.rx_pool_base;
    g_eth.rx_pool_inited = true;
    LOG1("[eth] RX pool init base=0x%08x bufsz=%u\n", (unsigned)g_eth.rx_pool_base, (unsigned)g_eth.rx_pool_bufsz);
  }

  hwaddr start = (hwaddr)g_eth.DMARDLAR;
  hwaddr cur   = start;

  for (int i = 0; i < 64; i++) {
    EthDesc40 d;
    if (!desc_read(cur, &d)) break;

    if (d.w[2] == 0) {
      hwaddr buf = align_up(g_eth.rx_pool_next, 32u);
      g_eth.rx_pool_next = buf + align_up((hwaddr)g_eth.rx_pool_bufsz, 32u);
      d.w[2] = (uint32_t)buf;
      (void)desc_write(cur, &d);
      LOG1("[eth] RX desc @0x%08x DESC2=0 -> set 0x%08x (cap=%u)\n",
           (unsigned)cur, (unsigned)d.w[2], (unsigned)g_eth.rx_pool_bufsz);
    }

    hwaddr nxt = next_desc_addr_rx(&d, cur);
    if (nxt == 0 || nxt == cur || nxt == start) break;
    cur = nxt;
  }
}

static bool deliver_one_rx(const uint8_t *pkt, uint32_t pkt_len)
{
  if (!g_eth.have_rx_base) return false;

  if (!g_eth.ignore_enable) {
    if (!mac_rx_enabled()) return false;
    if (!dma_rx_started()) return false;
  }

  rx_fixup_ring_buffers();

  if (!looks_like_sram_wordptr((uint32_t)g_eth.rx_cur))
    g_eth.rx_cur = (hwaddr)g_eth.DMARDLAR;
  if (!looks_like_sram_wordptr((uint32_t)g_eth.rx_cur))
    return false;

  EthDesc40 d;
  if (!desc_read(g_eth.rx_cur, &d)) return false;

  uint32_t des0 = d.w[0];

  // Real DMA: if CPU owns it (OWN=0), STOP and set RU. Do NOT skip forward.
  if ((des0 & DESC_OWN_BIT) == 0) {
    g_eth.DMASR |= (DMASR_RU | DMASR_AIS);   // RU is “abnormal”
    return false;
  }

  hwaddr buf_addr = (hwaddr)(uint64_t)d.w[2];
  if (!looks_like_sram_addr((uint32_t)buf_addr)) {
    g_eth.DMASR |= (DMASR_RU | DMASR_AIS);
    return false;
  }

  uint32_t cap = rx_bufcap_from_des1(d.w[1]);
  if (cap == 0 || cap > ETH_MAX_FRAME) cap = ETH_MAX_FRAME;

  uint32_t copy_len = (pkt_len <= cap) ? pkt_len : cap;

  if (mem_write(buf_addr, pkt, (int)copy_len) != 0) {
    g_eth.DMASR |= (DMASR_RU | DMASR_AIS);
    return false;
  }

  // HAL expects FL includes CRC (+4); HAL subtracts 4.
  uint32_t fl_with_crc = copy_len + 4u;
  if (fl_with_crc > 0x3FFFu) fl_with_crc = 0x3FFFu;

  uint32_t new_des0 = des0 & ~DESC_OWN_BIT;              // hand to CPU
  new_des0 |= (RXDESC_FS_BIT | RXDESC_LS_BIT);
  new_des0 &= ~RXDESC_FL_MASK;
  new_des0 |= ((fl_with_crc & 0x3FFFu) << RXDESC_FL_SHIFT);

  d.w[0] = new_des0;
  (void)desc_write(g_eth.rx_cur, &d);

  // Clear RU if we just made progress
  g_eth.DMASR &= ~DMASR_RU;
  g_eth.DMASR |= (DMASR_RI | DMASR_NIS);

  g_eth.seen_rx_frames++;
  LOG1("[eth] RX delivered len=%u (FL=%u) desc=0x%08x buf=0x%08x D0=%08x\n",
       (unsigned)copy_len, (unsigned)fl_with_crc, (unsigned)g_eth.rx_cur,
       (unsigned)buf_addr, (unsigned)new_des0);

  hwaddr nxt = next_desc_addr_rx(&d, g_eth.rx_cur);
  if (looks_like_sram_wordptr((uint32_t)nxt) && nxt != g_eth.rx_cur) g_eth.rx_cur = nxt;
  return true;
}

static bool deliver_ipv4_frag_pair(uint8_t *frame, size_t frame_len, const TcpMeta *tm)
{
  size_t l3_off = tm->l3_off;
  if (frame_len < l3_off + tm->ip_hlen) return true;

  uint8_t *ip = frame + l3_off;

  // Only if not already fragmented
  uint16_t fo = rd_be16_u2(&ip[6]);
  if ((fo & 0x1FFFu) != 0) return true;        // non-zero offset
  if (fo & 0x2000u) return true;               // MF already set

  // Don’t fight DF; just skip
  if (fo & 0x4000u) return true;

  // IP payload length
  uint32_t ip_tot = tm->ip_tot;
  if (ip_tot < tm->ip_hlen) return true;
  uint32_t ip_pl = ip_tot - tm->ip_hlen;
  if (ip_pl < (uint32_t)(tm->tcp_hlen + 16)) return true; // too small to matter

  // Split inside IP payload, must be multiple of 8 for fragment offset
  uint32_t split = ip_pl / 2;
  split &= ~7u;
  if (split < 8 || split + 8 > ip_pl) return true;

  uint8_t f1[ETH_MAX_FRAME];
  uint8_t f2[ETH_MAX_FRAME];

  // Frame sizes
  uint32_t ip_tot_1 = tm->ip_hlen + split;
  uint32_t ip_tot_2 = tm->ip_hlen + (ip_pl - split);
  size_t len1 = l3_off + ip_tot_1;
  size_t len2 = l3_off + ip_tot_2;
  if (len1 > sizeof(f1) || len2 > sizeof(f2)) return true;

  // Copy L2 + IP header
  memcpy(f1, frame, l3_off + tm->ip_hlen);
  memcpy(f2, frame, l3_off + tm->ip_hlen);

  uint8_t *ip1 = f1 + l3_off;
  uint8_t *ip2 = f2 + l3_off;

  // Copy IP payload slices
  memcpy(ip1 + tm->ip_hlen, ip + tm->ip_hlen, split);
  memcpy(ip2 + tm->ip_hlen, ip + tm->ip_hlen + split, ip_pl - split);

  // Total lengths
  wr_be16(&ip1[2], (uint16_t)ip_tot_1);
  wr_be16(&ip2[2], (uint16_t)ip_tot_2);

  // Flags/offset: clear DF, set MF on first, set offset on second
  uint16_t fo1 = (uint16_t)(0x2000u | 0u);                 // MF=1, off=0
  uint16_t fo2 = (uint16_t)(0x0000u | (split >> 3));       // MF=0, off=split/8
  wr_be16(&ip1[6], fo1);
  wr_be16(&ip2[6], fo2);

  // Fix IP header checksum (TCP checksum unchanged; it’s on reassembled TCP segment)
  ipv4_fix_hdr_checksum(ip1, ip_tot_1);
  ipv4_fix_hdr_checksum(ip2, ip_tot_2);

  LOG1("[eth] INJ: IPv4 fragments split=%u len1=%u len2=%u\n",
       (unsigned)split, (unsigned)len1, (unsigned)len2);

  // Deliver both
  if (!deliver_one_rx(f1, (uint32_t)len1)) return false;
  if (!deliver_one_rx(f2, (uint32_t)len2)) return false;
  return true;
}

static bool deliver_tcp_split(uint8_t *frame, size_t frame_len,
                              const TcpMeta *tm,
                              size_t pay_off, size_t pay_len,
                              bool reorder)
{
  size_t l3_off = tm->l3_off;
  if (frame_len < pay_off + pay_len) return true;

  // Choose a cut inside payload (don’t be tiny)
  size_t cut = pay_len / 2;
  if (cut < 16) return true;
  if (cut > 512) cut = 512;          // keep it bounded for speed/stability

  // Build seg1: payload[0:cut]
  // Build seg2: payload[cut:pay_len], seq += cut

  uint8_t s1[ETH_MAX_FRAME];
  uint8_t s2[ETH_MAX_FRAME];

  // TCP header starts at l3_off + ip_hlen
  size_t tcp_off = l3_off + tm->ip_hlen;
  if (frame_len < tcp_off + tm->tcp_hlen) return true;

  // Segment lengths
  uint16_t ip_tot_1 = (uint16_t)(tm->ip_hlen + tm->tcp_hlen + cut);
  uint16_t ip_tot_2 = (uint16_t)(tm->ip_hlen + tm->tcp_hlen + (pay_len - cut));

  size_t len1 = l3_off + ip_tot_1;
  size_t len2 = l3_off + ip_tot_2;
  if (len1 > sizeof(s1) || len2 > sizeof(s2)) return true;

  // Copy headers up to TCP header
  memcpy(s1, frame, tcp_off + tm->tcp_hlen);
  memcpy(s2, frame, tcp_off + tm->tcp_hlen);

  uint8_t *ip1  = s1 + l3_off;
  uint8_t *ip2  = s2 + l3_off;
  uint8_t *tcp1 = s1 + tcp_off;
  uint8_t *tcp2 = s2 + tcp_off;

  // Copy payload pieces
  memcpy(s1 + (tcp_off + tm->tcp_hlen), frame + pay_off, cut);
  memcpy(s2 + (tcp_off + tm->tcp_hlen), frame + pay_off + cut, pay_len - cut);

  // Update IP total len
  wr_be16(&ip1[2], ip_tot_1);
  wr_be16(&ip2[2], ip_tot_2);

  // Update TCP seq for seg2
  uint32_t seq2 = tm->seq + (uint32_t)cut;
  wr_be32_u(&tcp2[4], seq2);

  // Fix checksums for both (IP + TCP)
  ipv4_fix_hdr_checksum(ip1, ip_tot_1);
  ipv4_fix_l4_checksum(ip1, ip_tot_1);

  ipv4_fix_hdr_checksum(ip2, ip_tot_2);
  ipv4_fix_l4_checksum(ip2, ip_tot_2);

  LOG1("[eth] INJ: TCP split cut=%u reorder=%u\n", (unsigned)cut, reorder ? 1u : 0u);

  if (!reorder) {
    if (!deliver_one_rx(s1, (uint32_t)len1)) return false;
    if (!deliver_one_rx(s2, (uint32_t)len2)) return false;
  } else {
    // out-of-order: stresses reassembly queues
    if (!deliver_one_rx(s2, (uint32_t)len2)) return false;
    if (!deliver_one_rx(s1, (uint32_t)len1)) return false;
  }
  return true;
}

static bool rx_attack_and_deliver(uint8_t *buf, size_t n)
{
  // Work on a local copy so we can deliver variants
  uint8_t frame[ETH_MAX_FRAME];
  if (n > sizeof(frame)) n = sizeof(frame);
  memcpy(frame, buf, n);

  // Apply your existing HTTP mutation (gated by fuzz_every_n)
  maybe_fuzz_rx_frame(frame, n);

  // Parse again to get offsets/meta
  size_t pay_off = 0, pay_len = 0;
  TcpMeta tm;
  if (!parse_ipv4_tcp_payload(frame, n, &pay_off, &pay_len, &tm)) {
    return deliver_one_rx(frame, (uint32_t)n);
  }

  // Only attack tcp:80 with http-ish payload; otherwise just deliver
  if (tm.dport != 80 || pay_len == 0 || !looks_like_http_req(frame + pay_off, pay_len)) {
    return deliver_one_rx(frame, (uint32_t)n);
  }

  g_eth.rx_http_seen++;

  // Very low-rate “bad header” hook (optional). Keep it rare.
  if (g_eth.inj_badhdr_every_n && (g_eth.rx_http_seen % g_eth.inj_badhdr_every_n) == 0) {
    // Example: TTL=0 (usually drop) + fix IP checksum
    uint8_t *ip = frame + tm.l3_off;
    ip[8] = 0; // TTL
    ipv4_fix_hdr_checksum(ip, tm.ip_tot);
    LOG1("[eth] INJ: badhdr TTL=0\n");
    return deliver_one_rx(frame, (uint32_t)n);
  }

  // IP fragmentation
  if (g_eth.inj_frag_every_n && (g_eth.rx_http_seen % g_eth.inj_frag_every_n) == 0) {
    if (!deliver_ipv4_frag_pair(frame, n, &tm)) return false;
    return true;
  }

  // TCP split + rare reorder
  if (g_eth.inj_split_every_n && (g_eth.rx_http_seen % g_eth.inj_split_every_n) == 0) {
    bool reorder = (g_eth.inj_reorder_every_n &&
                    (g_eth.rx_http_seen % g_eth.inj_reorder_every_n) == 0);
    if (!deliver_tcp_split(frame, n, &tm, pay_off, pay_len, reorder)) return false;
    return true;
  }

  // Normal deliver
  if (!deliver_one_rx(frame, (uint32_t)n)) return false;

  // Optional duplicate (low-rate)
  if (g_eth.inj_dup_every_n && (g_eth.rx_http_seen % g_eth.inj_dup_every_n) == 0) {
    LOG1("[eth] INJ: duplicate segment\n");
    if (!deliver_one_rx(frame, (uint32_t)n)) return false;
  }

  return true;
}

// ---- PHY / MII ----
// Many Cube BSP PHY drivers (LAN8742/DP83848/etc.) read reg 31 "special status".
// So we set BOTH BSR (reg1) and reg31 to link-up-ish values.
static void phy_init_defaults(void)
{
  memset(g_eth.phy_regs, 0, sizeof(g_eth.phy_regs));

  // Standard clause-22 regs
  g_eth.phy_regs[0]  = 0x3100; // BMCR: autoneg enable-ish
  g_eth.phy_regs[1]  = 0x786D; // BMSR: link up + autoneg complete + capabilities

  // IDs (keep whatever you want; most BSPs don't strictly gate on it)
  g_eth.phy_regs[2]  = 0x0007;
  g_eth.phy_regs[3]  = 0xC0F1;

  // Vendor/status regs commonly used by BSP link-state logic
  // REG16 often used as PHYSTS (DP83848-style): bit0=link up in many drivers.
  // REG25 sometimes used as another status/control reg; some BSPs check it too.
  g_eth.phy_regs[16] = 0x0017; // bit0=1 (link), plus some nonzero speed/duplex/autoneg bits
  g_eth.phy_regs[25] = 0x0001; // "link good" (nonzero + bit0)

  // Keep reg31 nonzero too (covers LAN8742-style drivers if they ever read it)
  g_eth.phy_regs[31] = 0x001D;
}

static void mii_start_transaction(void)
{
  // STM32 MACMIIAR: PA[15:11], MR[10:6], MW[1], MB[0]
  uint32_t miiar = g_eth.MACMIIAR;
  uint8_t pa  = (uint8_t)((miiar >> 11) & 0x1Fu);
  uint8_t reg = (uint8_t)((miiar >> 6)  & 0x1Fu);
  bool is_write = ((miiar >> 1) & 1u) != 0;

  uint16_t oldv = 0;
  uint16_t newv = 0;

  if ((pa == 0 || pa == 1) && reg < 32) {
    oldv = g_eth.phy_regs[reg];
    if (is_write) {
      newv = (uint16_t)(g_eth.MACMIIDR & 0xFFFFu);
      g_eth.phy_regs[reg] = newv;
    } else {
      g_eth.MACMIIDR = (uint32_t)g_eth.phy_regs[reg];
      newv = (uint16_t)(g_eth.MACMIIDR & 0xFFFFu);
    }
  } else {
    if (!is_write) g_eth.MACMIIDR = 0;
  }

  if (g_log_level >= 2) {
    LOG2("[eth] MII %s PA=%u REG=%u 0x%04x%s\n",
         is_write ? "W" : "R",
         (unsigned)pa, (unsigned)reg,
         (unsigned)(is_write ? newv : newv),
         is_write ? "" : "");
    if (is_write) {
      LOG2("[eth]     PHY[%u]: %04x -> %04x\n", (unsigned)reg, (unsigned)oldv, (unsigned)newv);
    }
  }

  // Clear busy
  g_eth.MACMIIAR &= ~1u;
}

// ---- reset ----
static void dma_soft_reset(void)
{
  g_eth.DMAOMR = 0;
  g_eth.DMASR  = 0;
  g_eth.DMARPDR = 0;
  g_eth.DMATPDR = 0;
  g_eth.DMARDLAR = 0;
  g_eth.DMATDLAR = 0;

  g_eth.have_rx_base = g_eth.have_tx_base = false;
  g_eth.rx_cur = g_eth.tx_cur = 0;

  g_eth.rx_pool_inited = false;
  g_eth.rx_pool_base = g_eth.rx_pool_next = 0;
  g_eth.rx_pool_bufsz = 0;

  g_eth.dumped_rings_once = false;
  LOG1("[eth] DMA soft reset\n");
}

// ---- tiny ethernet header parse for logging/filtering ----
static void log_tap_pkt_brief(const uint8_t *eth, int eth_len)
{
  if (eth_len < 14) return;
  uint16_t type = (uint16_t)((eth[12] << 8) | eth[13]);
  const uint8_t *dst = eth + 0;
  LOG2("[eth] tap rx n=%d dst=%02x:%02x:%02x:%02x:%02x:%02x type=%04x\n",
       eth_len,
       dst[0],dst[1],dst[2],dst[3],dst[4],dst[5],
       (unsigned)type);
}

static void maca0_get(uint8_t mac[6])
{
  uint32_t lr = g_eth.MACA0LR;
  uint32_t hr = g_eth.MACA0HR;
  mac[0] = (uint8_t)(lr & 0xFF);
  mac[1] = (uint8_t)((lr >> 8) & 0xFF);
  mac[2] = (uint8_t)((lr >> 16) & 0xFF);
  mac[3] = (uint8_t)((lr >> 24) & 0xFF);
  mac[4] = (uint8_t)(hr & 0xFF);
  mac[5] = (uint8_t)((hr >> 8) & 0xFF);
}

static bool should_accept_frame(const uint8_t *eth, int eth_len)
{
  if (eth_len < 14) return false;

  // before promisc check:
 if ((eth[0] & 1) && !(eth[0]==0xff && eth[1]==0xff && eth[2]==0xff && eth[3]==0xff && eth[4]==0xff && eth[5]==0xff))
 return false;

  if (g_eth.promisc) return true;

  const uint8_t *dst = eth;
  static const uint8_t bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

  // Always accept broadcast (ARP will be broadcast)
  if (memcmp(dst, bcast, 6) == 0) return true;

  // Accept only unicast to our MAC
  uint8_t mymac[6];
  maca0_get(mymac);
  // If MAC isn't configured yet, be conservative: only broadcast allowed
  bool mac_set = false;
  for (int i = 0; i < 6; i++) if (mymac[i] != 0) { mac_set = true; break; }
  if (!mac_set) return false;

  return memcmp(dst, mymac, 6) == 0;
}

static void tx_scan_and_send(void)
{
  if (!g_eth.have_tx_base) return;
  if (g_eth.tap_fd < 0) return;

  if (!g_eth.ignore_enable) {
    if (!mac_tx_enabled()) return;
    if (!dma_tx_started()) return;
  }

  if (!looks_like_sram_wordptr((uint32_t)g_eth.tx_cur)) g_eth.tx_cur = (hwaddr)g_eth.DMATDLAR;
  if (!looks_like_sram_wordptr((uint32_t)g_eth.tx_cur)) return;

  uint32_t first_des0 = 0;

  uint8_t frame[ETH_MAX_FRAME];
  uint32_t frame_len = 0;
  bool in_frame = false;

  for (int i = 0; i < 64; i++) {
    EthDesc40 d;
    if (!desc_read(g_eth.tx_cur, &d)) break;

    uint32_t des0 = d.w[0];
    uint32_t des1 = d.w[1];

    // Hardware stops at first CPU-owned descriptor
    if ((des0 & DESC_OWN_BIT) == 0) break;

    bool fs = (des0 & TXDESC_FS_BIT) != 0;
    bool ls = (des0 & TXDESC_LS_BIT) != 0;

    uint32_t len1 = des1 & TXDESC_TBS1_MASK;
    uint32_t len2 = (des1 >> 16) & 0x1FFFu;

    bool tch = (des0 & TXDESC_TCH_BIT) != 0;   // NOTE: TCH is in DESC0

    hwaddr buf1 = (hwaddr)(uint64_t)d.w[2];
    hwaddr buf2_or_next = (hwaddr)(uint64_t)d.w[3];

    if (fs) { frame_len = 0; in_frame = true; first_des0 = des0; }
    if (!in_frame) { frame_len = 0; in_frame = true; } // be forgiving

    // Copy buffer 1
    if (len1 && len1 <= ETH_MAX_FRAME && looks_like_sram_addr((uint32_t)buf1)) {
      uint32_t room = ETH_MAX_FRAME - frame_len;
      uint32_t take = (len1 <= room) ? len1 : room;
      if (take && mem_read(buf1, frame + frame_len, (int)take) == 0) {
        frame_len += take;
      }
    }

    // Copy buffer 2 only if NOT chained descriptor mode (otherwise DESC3 is next desc pointer)
    if (!tch && len2 && frame_len < ETH_MAX_FRAME && looks_like_sram_addr((uint32_t)buf2_or_next)) {
      uint32_t room = ETH_MAX_FRAME - frame_len;
      uint32_t take = (len2 <= room) ? len2 : room;
      if (take && mem_read(buf2_or_next, frame + frame_len, (int)take) == 0) {
        frame_len += take;
      }
    }

    // Clear OWN so HAL can progress even if we later drop the frame
    d.w[0] = des0 & ~DESC_OWN_BIT;
    (void)desc_write(g_eth.tx_cur, &d);

    hwaddr nxt = next_desc_addr_tx(&d, g_eth.tx_cur);
    if (!looks_like_sram_wordptr((uint32_t)nxt) || nxt == g_eth.tx_cur) {
      // can't advance safely; if this was LS, we can still try to send
      nxt = 0;
    }

    if (ls) {
    //   if (frame_len > 0) {
    //     tx_apply_hw_csum(first_des0, frame, frame_len);
    //     uint32_t send_len = frame_len;
    //     if (send_len < 60) {
    //     memset(frame + send_len, 0, 60 - send_len);
    //     send_len = 60;
    //     }
    //     api_tap_send(g_eth.tap_fd, frame, (int)send_len);
    //     g_eth.sent_tx_frames++;
    //     LOG1("[eth] TX sent frame_len=%u send_len=%u last_desc=0x%08x\n",
    //         (unsigned)frame_len, (unsigned)send_len, (unsigned)g_eth.tx_cur);
    //   }
    if (frame_len > 0) {
        // Option A: Keep relying on descriptors (risky if firmware is lazy)
        tx_apply_hw_csum(first_des0, frame, frame_len);

        // Option B (RECOMMENDED): Force valid checksums for the host OS
        // This ensures the packet is valid regardless of what the firmware requested.
        eth_fixup_tx_checksums(frame, frame_len);

        uint32_t send_len = frame_len;
        if (send_len < 60) {
            memset(frame + send_len, 0, 60 - send_len);
            send_len = 60;
        }

        api_tap_send(g_eth.tap_fd, frame, (int)send_len);
        g_eth.sent_tx_frames++;
        LOG1("[eth] TX sent frame_len=%u send_len=%u last_desc=0x%08x\n",
            (unsigned)frame_len, (unsigned)send_len, (unsigned)g_eth.tx_cur);
      }

    g_eth.DMASR |= (DMASR_TI | DMASR_NIS);
      in_frame = false;
      frame_len = 0;
    }

    if (!nxt) break;
    g_eth.tx_cur = nxt;
  }
}

static void eth_periodic_poll(void *opaque)
{
  (void)opaque;
  g_eth.poll_ticks++;

  // One-time dump when bases appear
  dump_first_descs_once();

  // TX then RX
  tx_scan_and_send();

  if (g_eth.tap_fd < 0) return;
  if (!g_eth.have_rx_base) return;

  uint8_t buf[ETH_MAX_FRAME];

  for (int i = 0; i < 16; i++) {
    int n = api_tap_recv_nonblock(g_eth.tap_fd, buf, (int)sizeof(buf));
    if (n <= 0) break;

    g_eth.tap_pkts++;
    g_eth.tap_bytes += (uint32_t)n;

    if (g_log_level >= 2) log_tap_pkt_brief(buf, n);

    if (!should_accept_frame(buf, n)) continue;

    if (!rx_attack_and_deliver(buf, (size_t)n)) break;
  }

  // Heartbeat
  if ((g_eth.poll_ticks % 200u) == 0u) { // ~1s if 5ms poll
    LOG1("[eth] alive: tap_pkts=%u tap_bytes=%u rx=%u tx=%u MACCR=%08x DMAOMR=%08x DMASR=%08x rx_cur=%08x tx_cur=%08x\n",
         g_eth.tap_pkts, g_eth.tap_bytes, g_eth.seen_rx_frames, g_eth.sent_tx_frames,
         g_eth.MACCR, g_eth.DMAOMR, g_eth.DMASR,
         (unsigned)g_eth.rx_cur, (unsigned)g_eth.tx_cur);
  }
}

// ---- MMIO dispatch ----
static bool is_in_range(hwaddr addr, hwaddr base, uint32_t span) { return (addr >= base) && (addr < (base + span)); }

static bool handle_mac_read(hwaddr addr, unsigned size, uint64_t *out)
{
  hwaddr off = addr - ETH_MAC_BASE;
  unsigned byte_off = (unsigned)(addr & 3u);
  switch ((uint32_t)off) {
    case MACCR_OFF:    *out = subread_u32(g_eth.MACCR, size, byte_off); return true;
    case MACMIIAR_OFF: *out = subread_u32(g_eth.MACMIIAR, size, byte_off); return true;
    case MACMIIDR_OFF: *out = subread_u32(g_eth.MACMIIDR, size, byte_off); return true;
    case MACIMR_OFF:   *out = subread_u32(g_eth.MACIMR, size, byte_off); return true;
    case MACA0HR_OFF:  *out = subread_u32(g_eth.MACA0HR, size, byte_off); return true;
    case MACA0LR_OFF:  *out = subread_u32(g_eth.MACA0LR, size, byte_off); return true;
    default: return false;
  }
}
static bool handle_mmc_read(hwaddr addr, unsigned size, uint64_t *out)
{
  hwaddr off = addr - ETH_MMC_BASE;
  unsigned byte_off = (unsigned)(addr & 3u);
  switch ((uint32_t)off) {
    case MMCRIMR_OFF: *out = subread_u32(g_eth.MMCRIMR, size, byte_off); return true;
    case MMCTIMR_OFF: *out = subread_u32(g_eth.MMCTIMR, size, byte_off); return true;
    default: return false;
  }
}
static bool handle_dma_read(hwaddr addr, unsigned size, uint64_t *out)
{
  hwaddr off = addr - ETH_DMA_BASE;
  unsigned byte_off = (unsigned)(addr & 3u);
  switch ((uint32_t)off) {
    case DMABMR_OFF:   *out = subread_u32(g_eth.DMABMR, size, byte_off); return true;
    case DMATPDR_OFF:  *out = subread_u32(g_eth.DMATPDR, size, byte_off); return true;
    case DMARPDR_OFF:  *out = subread_u32(g_eth.DMARPDR, size, byte_off); return true;
    case DMARDLAR_OFF: *out = subread_u32(g_eth.DMARDLAR, size, byte_off); return true;
    case DMATDLAR_OFF: *out = subread_u32(g_eth.DMATDLAR, size, byte_off); return true;
    case DMASR_OFF:    *out = subread_u32(g_eth.DMASR, size, byte_off); return true;
    case DMAOMR_OFF:   *out = subread_u32(g_eth.DMAOMR, size, byte_off); return true;
    default: return false;
  }
}

static bool handle_mac_write(hwaddr addr, uint64_t value, unsigned size)
{
  hwaddr off = addr - ETH_MAC_BASE;
  unsigned byte_off = (unsigned)(addr & 3u);

  switch ((uint32_t)off) {
    case MACCR_OFF: {
      uint32_t old = g_eth.MACCR;
      g_eth.MACCR = merge_subwrite_u32(g_eth.MACCR, value, size, byte_off);
      LOG1("[eth] MACCR <= 0x%08x (RE=%u TE=%u)\n",
           g_eth.MACCR, (g_eth.MACCR & MACCR_RE_BIT) ? 1u : 0u, (g_eth.MACCR & MACCR_TE_BIT) ? 1u : 0u);
      if (g_log_level >= 2 && old != g_eth.MACCR) {
        // nothing more for now
      }
      return true;
    }
    case MACIMR_OFF:
      g_eth.MACIMR = merge_subwrite_u32(g_eth.MACIMR, value, size, byte_off);
      return true;

    case MACMIIDR_OFF:
      g_eth.MACMIIDR = merge_subwrite_u32(g_eth.MACMIIDR, value, size, byte_off);
      return true;

    case MACMIIAR_OFF: {
      uint32_t neu = merge_subwrite_u32(g_eth.MACMIIAR, value, size, byte_off);
      g_eth.MACMIIAR = neu;
      // If busy set, complete transaction immediately and clear busy.
      if (neu & 1u) mii_start_transaction();
      return true;
    }

    case MACA0HR_OFF:
      g_eth.MACA0HR = merge_subwrite_u32(g_eth.MACA0HR, value, size, byte_off);
      LOG1("[eth] MACA0HR <= 0x%08x\n", g_eth.MACA0HR);
      return true;

    case MACA0LR_OFF:
      g_eth.MACA0LR = merge_subwrite_u32(g_eth.MACA0LR, value, size, byte_off);
      LOG1("[eth] MACA0LR <= 0x%08x\n", g_eth.MACA0LR);
      return true;

    default:
      return false;
  }
}

static bool handle_mmc_write(hwaddr addr, uint64_t value, unsigned size)
{
  hwaddr off = addr - ETH_MMC_BASE;
  unsigned byte_off = (unsigned)(addr & 3u);
  switch ((uint32_t)off) {
    case MMCRIMR_OFF: g_eth.MMCRIMR = merge_subwrite_u32(g_eth.MMCRIMR, value, size, byte_off); return true;
    case MMCTIMR_OFF: g_eth.MMCTIMR = merge_subwrite_u32(g_eth.MMCTIMR, value, size, byte_off); return true;
    default: return false;
  }
}

static bool handle_dma_write(hwaddr addr, uint64_t value, unsigned size)
{
  hwaddr off = addr - ETH_DMA_BASE;
  unsigned byte_off = (unsigned)(addr & 3u);

  switch ((uint32_t)off) {
    case DMABMR_OFF: {
      uint32_t neu = merge_subwrite_u32(g_eth.DMABMR, value, size, byte_off);
      if (neu & DMABMR_SWR) {
        dma_soft_reset();
        neu &= ~DMABMR_SWR; // auto-clear
      }
      g_eth.DMABMR = neu;
      LOG1("[eth] DMABMR <= 0x%08x\n", g_eth.DMABMR);
      return true;
    }

    case DMAOMR_OFF:
      g_eth.DMAOMR = merge_subwrite_u32(g_eth.DMAOMR, value, size, byte_off);
      LOG1("[eth] DMAOMR <= 0x%08x (SR=%u ST=%u)\n",
           g_eth.DMAOMR, (g_eth.DMAOMR & DMAOMR_SR) ? 1u : 0u, (g_eth.DMAOMR & DMAOMR_ST) ? 1u : 0u);
      return true;

    case DMATDLAR_OFF:
    g_eth.DMATDLAR = merge_subwrite_u32(g_eth.DMATDLAR, value, size, byte_off);
    if (looks_like_sram_wordptr(g_eth.DMATDLAR)) {
        g_eth.have_tx_base = true;

        // Only initialize tx_cur if it isn't valid yet
        if (!looks_like_sram_wordptr((uint32_t)g_eth.tx_cur)) {
        g_eth.tx_cur = (hwaddr)g_eth.DMATDLAR;
        }

        LOG1("[eth] DMATDLAR=0x%08x tx_cur=0x%08x\n",
            (unsigned)g_eth.DMATDLAR, (unsigned)g_eth.tx_cur);
    }
    return true;

    case DMARDLAR_OFF:
      g_eth.DMARDLAR = merge_subwrite_u32(g_eth.DMARDLAR, value, size, byte_off);
      if (looks_like_sram_wordptr(g_eth.DMARDLAR)) {
        g_eth.have_rx_base = true;
        g_eth.rx_cur = (hwaddr)g_eth.DMARDLAR;
        LOG1("[eth] DMARDLAR=0x%08x rx_cur=0x%08x\n", (unsigned)g_eth.DMARDLAR, (unsigned)g_eth.rx_cur);
        rx_fixup_ring_buffers();
      }
      return true;

    case DMATPDR_OFF: {
    g_eth.DMATPDR = merge_subwrite_u32(g_eth.DMATPDR, value, size, byte_off);

    // DMATPDR is a "poll demand" doorbell. Ignore the written value.
    // DO NOT: g_eth.tx_cur = (hwaddr)g_eth.DMATPDR;

    tx_scan_and_send();
    return true;
    }


    case DMARPDR_OFF:
      g_eth.DMARPDR = merge_subwrite_u32(g_eth.DMARPDR, value, size, byte_off);
      // Poll demand; some stacks poke this to recover from RU.
      g_eth.DMASR &= ~DMASR_RU;
      rx_fixup_ring_buffers();
      return true;

    case DMASR_OFF: {
      // W1C bits
      uint32_t w = (uint32_t)(value & 0xFFFFFFFFu);
      g_eth.DMASR &= ~(w & DMASR_W1C_MASK);
      return true;
    }

    default:
      return false;
  }
}

// ---- Public API expected by your loader ----
uint64_t ethernet_read(void *opaque, hwaddr addr, unsigned size)
{
  (void)opaque;
  uint64_t out = 0;

  if (is_in_range(addr, ETH_DMA_BASE, 0x1000)) { if (handle_dma_read(addr, size, &out)) return out; return 0; }
  if (is_in_range(addr, ETH_MMC_BASE, 0x100))  { if (handle_mmc_read(addr, size, &out)) return out; return 0; }
  if (is_in_range(addr, ETH_MAC_BASE, 0x2000)) { if (handle_mac_read(addr, size, &out)) return out; return 0; }
  return 0;
}

void ethernet_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
  (void)opaque;

  if (is_in_range(addr, ETH_DMA_BASE, 0x1000)) { (void)handle_dma_write(addr, value, size); return; }
  if (is_in_range(addr, ETH_MMC_BASE, 0x100))  { (void)handle_mmc_write(addr, value, size); return; }
  if (is_in_range(addr, ETH_MAC_BASE, 0x2000)) { (void)handle_mac_write(addr, value, size); return; }
}

void ethernet_init(ConfigSection* model_info)
{
  (void)model_info;
  memset(&g_eth, 0, sizeof(g_eth));

  // baseline-ish defaults
  g_eth.DMABMR = 0x00020100u;
  g_eth.MACCR  = 0x00008000u;

  phy_init_defaults();

  const char *lvl = getenv("ETH_LOG_LEVEL");
  if (lvl && lvl[0]) g_log_level = atoi(lvl);

  const char *poll = getenv("ETH_POLL_NS");
  uint64_t poll_ns = 5000000ull;
  if (poll && poll[0]) poll_ns = (uint64_t)strtoull(poll, NULL, 10);

  const char *pool_en = getenv("ETH_RX_POOL_ENABLE");
  g_eth.rx_pool_enable = true;
  if (pool_en && pool_en[0] == '0') g_eth.rx_pool_enable = false;

  const char *ign = getenv("ETH_IGNORE_ENABLE");
  g_eth.ignore_enable = (ign && ign[0] == '1');

  const char *pr = getenv("ETH_PROMISC");
  g_eth.promisc = true;
  if (pr && pr[0] == '0') g_eth.promisc = false;

  const char *ifname = getenv("ETH_TAP");
  if (!ifname || !ifname[0]) ifname = "tap0";

    const char *fz = getenv("ETH_FUZZ");
  g_eth.fuzz_enable = (fz && fz[0] == '1');

  const char *fe = getenv("ETH_FUZZ_EVERY_N");
  g_eth.fuzz_every_n = (fe && fe[0]) ? (uint32_t)strtoul(fe, NULL, 10) : 1;

  const char *fm = getenv("ETH_FUZZ_MUT_MAX");
  g_eth.fuzz_mut_max = (fm && fm[0]) ? (uint32_t)strtoul(fm, NULL, 10) : 16;

  const char *fs = getenv("ETH_FUZZ_SEED");
  g_eth.fuzz_seed = (fs && fs[0]) ? (uint32_t)strtoul(fs, NULL, 10) : 0xC0FFEEu;
  srand((unsigned)g_eth.fuzz_seed);

    const char *f = getenv("ETH_INJ_FRAG_EVERY_N");
    g_eth.inj_frag_every_n = (f && f[0]) ? (uint32_t)strtoul(f, NULL, 10) : 200;

    const char *s = getenv("ETH_INJ_SPLIT_EVERY_N");
    g_eth.inj_split_every_n = (s && s[0]) ? (uint32_t)strtoul(s, NULL, 10) : 50;

    const char *r = getenv("ETH_INJ_REORDER_EVERY_N");
    g_eth.inj_reorder_every_n = (r && r[0]) ? (uint32_t)strtoul(r, NULL, 10) : 300;

    const char *d = getenv("ETH_INJ_DUP_EVERY_N");
    g_eth.inj_dup_every_n = (d && d[0]) ? (uint32_t)strtoul(d, NULL, 10) : 80;

    const char *b = getenv("ETH_INJ_BADHDR_EVERY_N");
    g_eth.inj_badhdr_every_n = (b && b[0]) ? (uint32_t)strtoul(b, NULL, 10) : 500;


  LOG1("[eth] fuzz: enable=%u every_n=%u mut_max=%u seed=0x%x\n",
       g_eth.fuzz_enable ? 1u : 0u, g_eth.fuzz_every_n, g_eth.fuzz_mut_max, g_eth.fuzz_seed);

  g_eth.tap_fd = api_tap_init(ifname);
  if (g_eth.tap_fd >= 0) LOG1("[eth] TAP initialized: %s (fd=%d)\n", ifname, g_eth.tap_fd);
  else LOG1("[eth] TAP init failed (%s)\n", ifname);

  g_eth.poll_timer_id = qemu_plugin_timer_new_period_ns(eth_periodic_poll, NULL, poll_ns);

  LOG1("[eth] init complete (polling-only, no IRQ). log=%d rx_pool=%d promisc=%d ignore_enable=%d poll_ns=%llu\n",
       g_log_level, g_eth.rx_pool_enable ? 1 : 0, g_eth.promisc ? 1 : 0, g_eth.ignore_enable ? 1 : 0,
       (unsigned long long)poll_ns);
}
