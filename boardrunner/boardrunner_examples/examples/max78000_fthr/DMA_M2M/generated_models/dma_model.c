// Device Model for DMA
//
// Inferred Register Functions:
//   0x40028000 : DMA CTRL      (bit0 = global enable)
//   0x40028004 : DMA INTFL     (global interrupt flags, bit per channel)
//   0x40028100 + n*0x20 + 0x00 : CH_CFG
//   0x40028100 + n*0x20 + 0x04 : CH_STATUS (W1C, bit2 = done)
//   0x40028100 + n*0x20 + 0x08 : CH_SRC
//   0x40028100 + n*0x20 + 0x0C : CH_DST
//   0x40028100 + n*0x20 + 0x10 : CH_CNT
//   0x40028100 + n*0x20 + 0x14 : CH_SRC_RLD (unobserved, stored)
//   0x40028100 + n*0x20 + 0x18 : CH_DST_RLD (unobserved, stored)
//   0x40028100 + n*0x20 + 0x1C : CH_AUX / reload-related (observed as 0)
//
// Trace-backed behavior implemented:
//   - 16 channels, stride 0x20
//   - global enable at CTRL.bit0
//   - software-started RAM-to-RAM transfer when CH_CFG bit31 and bit0 are set
//   - completion sets CH_STATUS bit2 and global INTFL channel bit
//   - completion raises IRQ 28 (vector 0x2C => API argument 44)
//   - CH_STATUS is write-1-to-clear

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define DMA_BASE                0x40028000ULL
#define DMA_NUM_CHANNELS        16
#define DMA_CH_BASE             0x100
#define DMA_CH_STRIDE           0x20

#define DMA_REG_CTRL            0x000
#define DMA_REG_INTFL           0x004

#define DMA_CH_REG_CFG          0x00
#define DMA_CH_REG_STATUS       0x04
#define DMA_CH_REG_SRC          0x08
#define DMA_CH_REG_DST          0x0C
#define DMA_CH_REG_CNT          0x10
#define DMA_CH_REG_SRC_RLD      0x14
#define DMA_CH_REG_DST_RLD      0x18
#define DMA_CH_REG_AUX          0x1C

#define DMA_CTRL_EN             0x00000001u

#define DMA_CH_CFG_EN           0x80000000u
#define DMA_CH_CFG_START        0x00000001u

#define DMA_CH_STATUS_DONE      0x00000004u

// Trace shows IRQ vector 0x2C taken, which is external IRQ 28.
// qemu_plugin_raise_irq() must receive IRQ+16.
#define DMA_IRQ_NUM             28

typedef struct {
    uint32_t cfg;
    uint32_t status;
    uint32_t src;
    uint32_t dst;
    uint32_t cnt;
    uint32_t src_rld;
    uint32_t dst_rld;
    uint32_t aux;
    bool executed;
} DMAChannel;

typedef struct {
    uint32_t ctrl;
    DMAChannel ch[DMA_NUM_CHANNELS];
    int irq_num;
} DMAState;

static DMAState g_dma;

static void dma_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    dev_debug(buf);
}

static uint64_t dma_mask_read_value(uint32_t v, unsigned size)
{
    switch (size) {
    case 1:
        return v & 0xFFu;
    case 2:
        return v & 0xFFFFu;
    default:
        return v;
    }
}

static bool dma_decode_channel(hwaddr offset, int *ch_idx, hwaddr *ch_off)
{
    if (offset < DMA_CH_BASE) {
        return false;
    }

    hwaddr rel = offset - DMA_CH_BASE;
    int idx = (int)(rel / DMA_CH_STRIDE);
    hwaddr off = rel % DMA_CH_STRIDE;

    if (idx < 0 || idx >= DMA_NUM_CHANNELS) {
        return false;
    }

    *ch_idx = idx;
    *ch_off = off;
    return true;
}

static uint32_t dma_compute_intfl(DMAState *s)
{
    uint32_t v = 0;
    int i;

    for (i = 0; i < DMA_NUM_CHANNELS; i++) {
        if (s->ch[i].status & DMA_CH_STATUS_DONE) {
            v |= (1u << i);
        }
    }

    return v;
}

static void dma_raise_irq_if_needed(DMAState *s)
{
    if (dma_compute_intfl(s) != 0) {
        qemu_plugin_raise_irq(s->irq_num + 16, false);
    }
}

static void dma_do_copy(DMAState *s, int idx)
{
    DMAChannel *ch = &s->ch[idx];
    uint32_t remaining = ch->cnt;
    uint32_t src = ch->src;
    uint32_t dst = ch->dst;
    uint8_t buf[256];

    if (ch->executed) {
        return;
    }

    if (!(s->ctrl & DMA_CTRL_EN)) {
        return;
    }

    if (!(ch->cfg & DMA_CH_CFG_EN)) {
        return;
    }

    if (!(ch->cfg & DMA_CH_CFG_START)) {
        return;
    }

    // Observed case is RAM-to-RAM copy. If a future guest programs MMIO
    // addresses, RAM APIs will fail; we still complete to avoid hanging.
    while (remaining > 0) {
        int chunk = (remaining > sizeof(buf)) ? (int)sizeof(buf) : (int)remaining;
        int rd = qemu_plugin_read_memory(src, buf, chunk);
        if (rd != 0) {
            dma_log("DMA: failed read on ch%d src=0x%08x len=%d rd=%d",
                    idx, src, chunk, rd);
            break;
        }

        int wr = qemu_plugin_write_memory(dst, buf, chunk);
        if (wr != 0) {
            dma_log("DMA: failed write on ch%d dst=0x%08x len=%d wr=%d",
                    idx, dst, chunk, wr);
            break;
        }

        src += (uint32_t)chunk;
        dst += (uint32_t)chunk;
        remaining -= (uint32_t)chunk;
    }

    ch->status |= DMA_CH_STATUS_DONE;
    ch->executed = true;

    dma_raise_irq_if_needed(s);
}

static void dma_try_start_all(DMAState *s)
{
    int i;
    for (i = 0; i < DMA_NUM_CHANNELS; i++) {
        dma_do_copy(s, i);
    }
}

static void dma_reset_channel_runtime(DMAChannel *ch)
{
    ch->executed = false;
}

static uint32_t dma_read_reg(DMAState *s, hwaddr offset)
{
    int idx;
    hwaddr ch_off;

    switch (offset) {
    case DMA_REG_CTRL:
        return s->ctrl;
    case DMA_REG_INTFL:
        return dma_compute_intfl(s);
    default:
        break;
    }

    if (dma_decode_channel(offset, &idx, &ch_off)) {
        DMAChannel *ch = &s->ch[idx];

        switch (ch_off) {
        case DMA_CH_REG_CFG:
            return ch->cfg;
        case DMA_CH_REG_STATUS:
            return ch->status;
        case DMA_CH_REG_SRC:
            return ch->src;
        case DMA_CH_REG_DST:
            return ch->dst;
        case DMA_CH_REG_CNT:
            return ch->cnt;
        case DMA_CH_REG_SRC_RLD:
            return ch->src_rld;
        case DMA_CH_REG_DST_RLD:
            return ch->dst_rld;
        case DMA_CH_REG_AUX:
            return ch->aux;
        default:
            dma_log("DMA: unhandled read offset 0x%03llx", (unsigned long long)offset);
            return 0;
        }
    }

    dma_log("DMA: read outside register map offset 0x%03llx", (unsigned long long)offset);
    return 0;
}

// This function will emulate all device reads
uint64_t dma_read(void *opaque, hwaddr addr, unsigned size)
{
    DMAState *s = (DMAState *)opaque;
    hwaddr offset = addr - DMA_BASE;
    uint32_t v = dma_read_reg(s, offset);
    return dma_mask_read_value(v, size);
}

static void dma_write_reg(DMAState *s, hwaddr offset, uint32_t value)
{
    int idx;
    hwaddr ch_off;

    switch (offset) {
    case DMA_REG_CTRL:
        s->ctrl = value & DMA_CTRL_EN;
        dma_try_start_all(s);
        return;

    case DMA_REG_INTFL: {
        // Robustness: allow global W1C by channel bit.
        int i;
        for (i = 0; i < DMA_NUM_CHANNELS; i++) {
            if (value & (1u << i)) {
                s->ch[i].status &= ~DMA_CH_STATUS_DONE;
            }
        }
        return;
    }

    default:
        break;
    }

    if (dma_decode_channel(offset, &idx, &ch_off)) {
        DMAChannel *ch = &s->ch[idx];

        switch (ch_off) {
        case DMA_CH_REG_CFG:
            ch->cfg = value;
            if ((value & (DMA_CH_CFG_EN | DMA_CH_CFG_START)) !=
                (DMA_CH_CFG_EN | DMA_CH_CFG_START)) {
                ch->executed = false;
            }
            dma_do_copy(s, idx);
            return;

        case DMA_CH_REG_STATUS:
            // Observed as W1C: write 0 does nothing, write 4 clears done.
            ch->status &= ~value;
            return;

        case DMA_CH_REG_SRC:
            ch->src = value;
            dma_reset_channel_runtime(ch);
            return;

        case DMA_CH_REG_DST:
            ch->dst = value;
            dma_reset_channel_runtime(ch);
            return;

        case DMA_CH_REG_CNT:
            ch->cnt = value;
            dma_reset_channel_runtime(ch);
            return;

        case DMA_CH_REG_SRC_RLD:
            ch->src_rld = value;
            dma_reset_channel_runtime(ch);
            return;

        case DMA_CH_REG_DST_RLD:
            ch->dst_rld = value;
            dma_reset_channel_runtime(ch);
            return;

        case DMA_CH_REG_AUX:
            ch->aux = value;
            return;

        default:
            dma_log("DMA: unhandled write offset 0x%03llx val=0x%08x",
                    (unsigned long long)offset, value);
            return;
        }
    }

    dma_log("DMA: write outside register map offset 0x%03llx val=0x%08x",
            (unsigned long long)offset, value);
}

// This function will emulate all device writes
void dma_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    DMAState *s = (DMAState *)opaque;
    hwaddr offset = addr - DMA_BASE;
    uint32_t v;

    switch (size) {
    case 1:
        v = (uint32_t)(value & 0xFFu);
        break;
    case 2:
        v = (uint32_t)(value & 0xFFFFu);
        break;
    default:
        v = (uint32_t)value;
        break;
    }

    dma_write_reg(s, offset, v);
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* dma_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_dma, 0, sizeof(g_dma));
    g_dma.irq_num = DMA_IRQ_NUM;

    return &g_dma;
}