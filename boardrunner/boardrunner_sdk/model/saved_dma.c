// High-fidelity DMA1 (STM32F103xx) model focused on Channel 1:
// - Stable CCR1 readback (do NOT drop low bits like EN/TCIE/HTIE/TEIE).
// - Implements ISR/IFCR flags with W1C semantics.
// - Implements HT/TC generation based on CNDTR progression.
// - Implements circular mode (reload + pointer reset on TC).
// - Registers a DMA stream handler via api_dma_register_stream; peripherals call api_dma_request(stream_id).
//
// Assumptions (adjustable via macros):
// - DMA1 Channel1 IRQ line is 11 (STM32F103 NVIC mapping).
// - Stream id used by ADC1->DMA1_Channel1 is DMA1_CH1_STREAM_ID (default 0).
//
// Include <device.h> and <boardrunner/vio.h> for hwaddr, ConfigSection, dev_debug, qemu_plugin_* and api_dma_*.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#ifndef DMA1_BASE_ADDR
#define DMA1_BASE_ADDR 0x40020000ULL
#endif

#ifndef DMA1_CH1_IRQ
#define DMA1_CH1_IRQ 11+16
#endif

#ifndef DMA1_CH1_STREAM_ID
#define DMA1_CH1_STREAM_ID 0
#endif

// DMA1 register offsets
#define DMA_ISR_OFF   0x00
#define DMA_IFCR_OFF  0x04
#define DMA_CCR1_OFF  0x08
#define DMA_CNDTR1_OFF 0x0C
#define DMA_CPAR1_OFF  0x10
#define DMA_CMAR1_OFF  0x14

// Channel stride (not needed if we only implement CH1)
#define DMA_CH_STRIDE 0x14

// CCR bits (STM32F1)
#define CCR_EN      (1u << 0)
#define CCR_TCIE    (1u << 1)
#define CCR_HTIE    (1u << 2)
#define CCR_TEIE    (1u << 3)
#define CCR_DIR     (1u << 4)   // 0: P->M, 1: M->P
#define CCR_CIRC    (1u << 5)
#define CCR_PINC    (1u << 6)
#define CCR_MINC    (1u << 7)
#define CCR_PSIZE_SHIFT 8       // 2 bits
#define CCR_MSIZE_SHIFT 10      // 2 bits
#define CCR_PL_SHIFT    12      // 2 bits
#define CCR_MEM2MEM (1u << 14)

// Valid CCR mask (bits 0..14 used on STM32F1; bit15+ reserved)
#define CCR_VALID_MASK 0x7FFFu

// ISR/IFCR flags for Channel 1 (bits 0..3)
#define ISR_GIF1   (1u << 0)
#define ISR_TCIF1  (1u << 1)
#define ISR_HTIF1  (1u << 2)
#define ISR_TEIF1  (1u << 3)
#define ISR_CH1_MASK (ISR_GIF1 | ISR_TCIF1 | ISR_HTIF1 | ISR_TEIF1)

// -------- helper: dev_debug formatting
static void dbg(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

// -------- helper: partial accesses
static inline uint32_t read_slice32(uint32_t v, hwaddr off, unsigned size) {
    if (size == 4) return v;
    if (size == 2) {
        unsigned sh = (unsigned)((off & 2u) * 8u);
        return (v >> sh) & 0xFFFFu;
    }
    unsigned sh = (unsigned)((off & 3u) * 8u);
    return (v >> sh) & 0xFFu;
}

static inline uint32_t merge_write32(uint32_t oldv, uint64_t value, hwaddr off, unsigned size) {
    if (size == 4) return (uint32_t)value;
    if (size == 2) {
        unsigned sh = (unsigned)((off & 2u) * 8u);
        uint32_t mask = 0xFFFFu << sh;
        return (oldv & ~mask) | (((uint32_t)value & 0xFFFFu) << sh);
    }
    unsigned sh = (unsigned)((off & 3u) * 8u);
    uint32_t mask = 0xFFu << sh;
    return (oldv & ~mask) | (((uint32_t)value & 0xFFu) << sh);
}

static inline hwaddr dma_off(hwaddr addr) {
    uint64_t a = (uint64_t)addr;
    if (a >= DMA1_BASE_ADDR) return (hwaddr)(a - DMA1_BASE_ADDR);
    return addr;
}

// -------- size decode (PSIZE/MSIZE)
static inline unsigned sz_code_to_bytes(unsigned code) {
    // 00: 8-bit, 01: 16-bit, 10: 32-bit, 11: (treat as 32-bit)
    switch (code & 3u) {
        case 0: return 1;
        case 1: return 2;
        case 2: return 4;
        default: return 4;
    }
}

typedef struct DMA1Ch1 {
    uint32_t ccr1;
    uint32_t cndtr1;
    uint32_t cpar1;
    uint32_t cmar1;

    // internal runtime state
    uint32_t initial_ndtr;   // captured when EN transitions 0->1
    uint32_t cur_par;        // internal current address
    uint32_t cur_mar;        // internal current address
    bool active;
} DMA1Ch1;

typedef struct DMA1State {
    uint32_t isr;
    DMA1Ch1 ch1;
} DMA1State;

static DMA1State g_dma1;

// Forward decl: DMA request handler called via api_dma_request(stream_id)
static void dma1_ch1_request(void *opaque);

static void ch1_arm_if_needed(void) {
    DMA1Ch1 *ch = &g_dma1.ch1;
    bool en = (ch->ccr1 & CCR_EN) != 0;

    if (en && !ch->active) {
        ch->active = true;
        ch->initial_ndtr = ch->cndtr1;
        ch->cur_par = ch->cpar1;
        ch->cur_mar = ch->cmar1;
        // Do not auto-clear ISR flags; firmware clears via IFCR in traces.
    } else if (!en && ch->active) {
        ch->active = false;
    }
}

static void ch1_set_flag(uint32_t flag) {
    g_dma1.isr |= ISR_GIF1;   // hardware behavior in your trace: GIF stays set (they never clear it)
    g_dma1.isr |= flag;
}

static void ch1_maybe_raise_irq(bool ht_event, bool tc_event, bool te_event) {
    DMA1Ch1 *ch = &g_dma1.ch1;
    uint32_t ccr = ch->ccr1;

    bool fire = false;
    if (ht_event && (ccr & CCR_HTIE)) fire = true;
    if (tc_event && (ccr & CCR_TCIE)) fire = true;
    if (te_event && (ccr & CCR_TEIE)) fire = true;

    if (fire) {
        qemu_plugin_raise_irq(DMA1_CH1_IRQ, false);
    }
}

static void dma1_ch1_do_one_transfer(DMA1Ch1 *ch) {
    if (!ch->active) return;
    if (ch->cndtr1 == 0) return;

    uint32_t ccr = ch->ccr1;
    unsigned psize = sz_code_to_bytes((ccr >> CCR_PSIZE_SHIFT) & 3u);
    unsigned msize = sz_code_to_bytes((ccr >> CCR_MSIZE_SHIFT) & 3u);

    // For safety, limit to 4 bytes
    if (psize > 4) psize = 4;
    if (msize > 4) msize = 4;

    uint8_t buf[4] = {0};

    bool dir_m2p = (ccr & CCR_DIR) != 0; // 1 => memory-to-peripheral
    bool pinc = (ccr & CCR_PINC) != 0;
    bool minc = (ccr & CCR_MINC) != 0;

    bool te = false;

    if (!dir_m2p) {
        // Peripheral -> Memory (ADC typical)
        if (qemu_plugin_read_memory((unsigned long long)ch->cur_par, buf, (int)psize) < 0) {
            te = true;
        } else {
            // Write to memory (msize). If msize != psize, write low bytes.
            if (qemu_plugin_write_memory((unsigned long long)ch->cur_mar, buf, (int)msize) < 0) {
                te = true;
            }
        }
    } else {
        // Memory -> Peripheral
        if (qemu_plugin_read_memory((unsigned long long)ch->cur_mar, buf, (int)msize) < 0) {
            te = true;
        } else {
            if (qemu_plugin_write_memory((unsigned long long)ch->cur_par, buf, (int)psize) < 0) {
                te = true;
            }
        }
    }

    bool ht_event = false;
    bool tc_event = false;

    if (te) {
        ch1_set_flag(ISR_TEIF1);
        ch1_maybe_raise_irq(false, false, true);
        return;
    }

    // advance addresses
    if (pinc) ch->cur_par += psize;
    if (minc) ch->cur_mar += msize;

    // decrement NDTR by one item
    if (ch->cndtr1 > 0) ch->cndtr1--;

    // Half-transfer event: when remaining == initial/2 (common STM32 behavior)
    if (ch->initial_ndtr != 0) {
        uint32_t half = ch->initial_ndtr / 2u;
        if (half != 0 && ch->cndtr1 == half) {
            ht_event = true;
            ch1_set_flag(ISR_HTIF1);
        }
    }

    // Transfer-complete event
    if (ch->cndtr1 == 0) {
        tc_event = true;
        ch1_set_flag(ISR_TCIF1);

        // Circular mode reload
        if (ccr & CCR_CIRC) {
            ch->cndtr1 = ch->initial_ndtr;
            ch->cur_par = ch->cpar1;
            ch->cur_mar = ch->cmar1;
        } else {
            // Non-circular: channel typically remains enabled but no further transfers occur unless reprogrammed.
        }
    }

    ch1_maybe_raise_irq(ht_event, tc_event, false);
}

// Handler invoked by api_dma_request(DMA1_CH1_STREAM_ID)
static void dma1_ch1_request(void *opaque) {
    (void)opaque;
    dma1_ch1_do_one_transfer(&g_dma1.ch1);
}

// ---------- MMIO read/write ----------
uint64_t dma1_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque;
    hwaddr off = dma_off(addr);
    hwaddr reg = off & ~3u;

    uint32_t v = 0;

    switch (reg) {
        case DMA_ISR_OFF:
            v = g_dma1.isr;
            break;

        case DMA_IFCR_OFF:
            // Readback often returns 0 (write-only) on many MCUs; safe to return 0.
            v = 0;
            break;

        case DMA_CCR1_OFF:
            v = (g_dma1.ch1.ccr1 & CCR_VALID_MASK);
            break;

        case DMA_CNDTR1_OFF:
            v = g_dma1.ch1.cndtr1;
            break;

        case DMA_CPAR1_OFF:
            v = g_dma1.ch1.cpar1;
            break;

        case DMA_CMAR1_OFF:
            v = g_dma1.ch1.cmar1;
            break;

        default:
            v = 0;
            break;
    }

    return (uint64_t)read_slice32(v, off, size);
}

void dma1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque;
    hwaddr off = dma_off(addr);
    hwaddr reg = off & ~3u;

    switch (reg) {
        case DMA_IFCR_OFF: {
            // Write-1-to-clear for ISR flags.
            uint32_t w = (uint32_t)value;

            // Channel 1 clear bits are w[3:0] -> clear ISR_CH1 bits.
            if (w & ISR_GIF1)  g_dma1.isr &= ~ISR_GIF1;
            if (w & ISR_TCIF1) g_dma1.isr &= ~ISR_TCIF1;
            if (w & ISR_HTIF1) g_dma1.isr &= ~ISR_HTIF1;
            if (w & ISR_TEIF1) g_dma1.isr &= ~ISR_TEIF1;
            break;
        }

        case DMA_CCR1_OFF: {
            uint32_t old = g_dma1.ch1.ccr1;
            uint32_t newv = merge_write32(old, value, off, size);

            // Preserve only valid config bits; IMPORTANT: do NOT drop bits0..3.
            newv &= CCR_VALID_MASK;

            g_dma1.ch1.ccr1 = newv;

            // Track enable transitions
            ch1_arm_if_needed();
            break;
        }

        case DMA_CNDTR1_OFF:
            g_dma1.ch1.cndtr1 = merge_write32(g_dma1.ch1.cndtr1, value, off, size);
            // If channel already enabled, keep initial_ndtr unchanged; hardware typically latches on enable.
            break;

        case DMA_CPAR1_OFF:
            g_dma1.ch1.cpar1 = merge_write32(g_dma1.ch1.cpar1, value, off, size);
            break;

        case DMA_CMAR1_OFF:
            g_dma1.ch1.cmar1 = merge_write32(g_dma1.ch1.cmar1, value, off, size);
            break;

        case DMA_ISR_OFF:
            // write ignored (read-only)
            break;

        default:
            break;
    }
}

void dma1_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_dma1, 0, sizeof(g_dma1));

    // Register Channel1 stream handler so peripherals (ADC1) can trigger DMA requests.
    api_dma_register_stream(DMA1_CH1_STREAM_ID, dma1_ch1_request, &g_dma1);

    // dbg("DMA1 init: ch1_stream=%d irq=%d\n", DMA1_CH1_STREAM_ID, DMA1_CH1_IRQ);
}
