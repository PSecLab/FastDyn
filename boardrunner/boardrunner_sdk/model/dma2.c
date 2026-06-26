#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <device.h>
#include <boardrunner/vio.h>

#define DMA2_BASE           0x40026400ULL

#define DMA_LISR_OFF        0x00
#define DMA_HISR_OFF        0x04
#define DMA_LIFCR_OFF       0x08
#define DMA_HIFCR_OFF       0x0C
#define DMA_STREAM_BASE     0x10
#define DMA_STREAM_STRIDE   0x18

#define DMA_SXCR_OFF        0x00
#define DMA_SXNDTR_OFF      0x04
#define DMA_SXPAR_OFF       0x08
#define DMA_SXM0AR_OFF      0x0C
#define DMA_SXM1AR_OFF      0x10
#define DMA_SXFCR_OFF       0x14

#define DMA_SXCR_EN         (1U << 0)
#define DMA_SXCR_TCIE       (1U << 4)
#define DMA_SXCR_DIR_MASK   (3U << 6)
#define DMA_SXCR_DIR_P2M    (0U << 6)
#define DMA_SXCR_DIR_M2P    (1U << 6)
#define DMA_SXCR_CIRC       (1U << 8)
#define DMA_SXCR_MINC       (1U << 10)

#define DMA2_STREAM1_QSIZE  4096
#define DMA2_DATA_CHUNK     8

#define DMA2_LISR_TCIF1     (1U << 11)
#define DMA2_HISR_TCIF7     (1U << 27)

#define DMA2_STREAM1_IRQ    57
#define DMA2_STREAM7_IRQ    70

typedef struct {
    uint32_t cr_cfg;
    uint32_t ndtr;
    uint32_t par;
    uint32_t m0ar;
    uint32_t m1ar;
    uint32_t fcr;
    uint32_t initial_ndtr;
    uint32_t mem_index;
    bool enabled;
} DMA2StreamState;

typedef struct {
    uint32_t lisr;
    uint32_t hisr;
    DMA2StreamState stream[8];
    uint8_t s1_queue[DMA2_STREAM1_QSIZE];
    unsigned s1_q_head;
    unsigned s1_q_tail;
    unsigned s1_q_count;
} DMA2State;

static DMA2State g_dma2;

static void dma2_finish_stream(DMA2State *s, unsigned stream_id) {
    DMA2StreamState *st = &s->stream[stream_id];

    switch (stream_id) {
    case 1U:
        s->lisr |= DMA2_LISR_TCIF1;
        if ((st->cr_cfg & DMA_SXCR_TCIE) != 0U) {
            qemu_plugin_raise_irq(DMA2_STREAM1_IRQ + 16, false);
        }
        break;
    case 7U:
        s->hisr |= DMA2_HISR_TCIF7;
        if ((st->cr_cfg & DMA_SXCR_TCIE) != 0U) {
            qemu_plugin_raise_irq(DMA2_STREAM7_IRQ + 16, false);
        }
        break;
    default:
        break;
    }

    if ((st->cr_cfg & DMA_SXCR_CIRC) != 0U && st->initial_ndtr != 0U) {
        st->ndtr = st->initial_ndtr;
        st->mem_index = 0U;
    } else {
        st->enabled = false;
    }
}

static void dma2_signal_stream1(DMA2State *s) {
    DMA2StreamState *st = &s->stream[1];

    s->lisr |= DMA2_LISR_TCIF1;
    if ((st->cr_cfg & DMA_SXCR_TCIE) != 0U) {
        qemu_plugin_raise_irq(DMA2_STREAM1_IRQ + 16, false);
    }
}

static bool dma2_s1q_push(DMA2State *s, uint8_t v) {
    if (s->s1_q_count >= DMA2_STREAM1_QSIZE) {
        return false;
    }
    s->s1_queue[s->s1_q_head] = v;
    s->s1_q_head = (s->s1_q_head + 1U) % DMA2_STREAM1_QSIZE;
    s->s1_q_count++;
    return true;
}

static bool dma2_s1q_pop(DMA2State *s, uint8_t *out) {
    if (s->s1_q_count == 0U) {
        return false;
    }
    *out = s->s1_queue[s->s1_q_tail];
    s->s1_q_tail = (s->s1_q_tail + 1U) % DMA2_STREAM1_QSIZE;
    s->s1_q_count--;
    return true;
}

static void dma2_service_stream1(DMA2State *s) {
    DMA2StreamState *st = &s->stream[1];
    uint8_t byte;
    uint32_t addr;
    bool moved = false;

    while (st->enabled && st->ndtr > 0U && s->s1_q_count > 0U) {
        if (!dma2_s1q_pop(s, &byte)) {
            break;
        }

        addr = st->m0ar;
        if ((st->cr_cfg & DMA_SXCR_MINC) != 0U) {
            addr += st->mem_index;
        }

        if (qemu_plugin_write_memory(addr, &byte, 1) != 0) {
            break;
        }

        st->mem_index++;
        st->ndtr--;
        moved = true;

        if (st->ndtr == 0U) {
            dma2_finish_stream(s, 1U);
            moved = false;
        }
    }

    if (moved) {
        dma2_signal_stream1(s);
    }
}

static void dma2_service_stream7(DMA2State *s) {
    DMA2StreamState *st = &s->stream[7];
    uint8_t buf[DMA2_DATA_CHUNK];
    uint32_t addr;
    uint32_t chunk_u32;
    int chunk;
    int i;

    if (!st->enabled || st->ndtr == 0U) {
        return;
    }

    if ((st->cr_cfg & DMA_SXCR_DIR_MASK) != DMA_SXCR_DIR_M2P) {
        return;
    }

    while (st->enabled && st->ndtr > 0U) {
        chunk_u32 = st->ndtr;
        if (chunk_u32 > (uint32_t)sizeof(buf)) {
            chunk_u32 = (uint32_t)sizeof(buf);
        }
        chunk = (int)chunk_u32;

        if ((st->cr_cfg & DMA_SXCR_MINC) != 0U) {
            addr = st->m0ar + st->mem_index;
            if (qemu_plugin_read_memory(addr, buf, chunk) != 0) {
                break;
            }
            st->mem_index += chunk_u32;
        } else {
            addr = st->m0ar;
            for (i = 0; i < chunk; i++) {
                if (qemu_plugin_read_memory(addr, &buf[i], 1) != 0) {
                    chunk = i;
                    break;
                }
            }
            if (chunk <= 0) {
                break;
            }
            chunk_u32 = (uint32_t)chunk;
        }

        if (api_dma_request_data(2, 7, st->par, buf, chunk) < 0) {
            break;
        }

        st->ndtr -= chunk_u32;

        if (st->ndtr == 0U) {
            dma2_finish_stream(s, 7U);
        }
    }
}

static void dma2_stream1_data_handler(void *opaque, const uint8_t *data, int len) {
    DMA2State *s = (DMA2State *)opaque;
    int i;

    if (data == NULL || len <= 0) {
        return;
    }

    for (i = 0; i < len; i++) {
        dma2_s1q_push(s, data[i]);
    }

    dma2_service_stream1(s);
}

void* dma2_init(ConfigSection* model_info) {
    unsigned i;

    (void)model_info;

    memset(&g_dma2, 0, sizeof(g_dma2));
    for (i = 0; i < 8U; i++) {
        g_dma2.stream[i].fcr = 0x21U;
    }

    api_dma_register_stream_data(2, 1, dma2_stream1_data_handler, &g_dma2);
    dev_debug("dma2: stream1 RX and stream7 TX DMA paths ready\n");
    return &g_dma2;
}

uint64_t dma2_read(void *opaque, uint64_t addr, unsigned size) {
    DMA2State *s = (DMA2State *)opaque;
    uint64_t offset = addr - DMA2_BASE;
    unsigned stream_id;
    uint64_t stream_off;

    (void)size;

    if (offset == DMA_LISR_OFF) {
        return s->lisr;
    }
    if (offset == DMA_HISR_OFF) {
        return s->hisr;
    }

    if (offset >= DMA_STREAM_BASE) {
        stream_id = (unsigned)((offset - DMA_STREAM_BASE) / DMA_STREAM_STRIDE);
        stream_off = (offset - DMA_STREAM_BASE) % DMA_STREAM_STRIDE;
        if (stream_id < 8U) {
            DMA2StreamState *st = &s->stream[stream_id];
            switch (stream_off) {
            case DMA_SXCR_OFF:
                return st->enabled ? DMA_SXCR_EN : 0;
            case DMA_SXNDTR_OFF:
                return st->ndtr;
            case DMA_SXPAR_OFF:
                return st->par;
            case DMA_SXM0AR_OFF:
                return st->m0ar;
            case DMA_SXM1AR_OFF:
                return st->m1ar;
            case DMA_SXFCR_OFF:
                return st->fcr;
            default:
                return 0;
            }
        }
    }

    return 0;
}

void dma2_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    DMA2State *s = (DMA2State *)opaque;
    uint64_t offset = addr - DMA2_BASE;
    unsigned stream_id;
    uint64_t stream_off;

    (void)size;

    if (offset == DMA_LIFCR_OFF) {
        s->lisr &= ~((uint32_t)value);
        return;
    }
    if (offset == DMA_HIFCR_OFF) {
        s->hisr &= ~((uint32_t)value);
        return;
    }

    if (offset >= DMA_STREAM_BASE) {
        stream_id = (unsigned)((offset - DMA_STREAM_BASE) / DMA_STREAM_STRIDE);
        stream_off = (offset - DMA_STREAM_BASE) % DMA_STREAM_STRIDE;
        if (stream_id < 8U) {
            DMA2StreamState *st = &s->stream[stream_id];

            switch (stream_off) {
            case DMA_SXCR_OFF:
                if (((uint32_t)value & DMA_SXCR_EN) != 0U && ((uint32_t)value & ~DMA_SXCR_EN) == 0U) {
                    st->enabled = true;
                } else {
                    st->cr_cfg = ((uint32_t)value & ~DMA_SXCR_EN);
                    st->enabled = (((uint32_t)value & DMA_SXCR_EN) != 0U);
                    if (!st->enabled) {
                        st->mem_index = 0U;
                    }
                }
                if (stream_id == 1U) {
                    dma2_service_stream1(s);
                } else if (stream_id == 7U) {
                    dma2_service_stream7(s);
                }
                return;
            case DMA_SXNDTR_OFF:
                st->ndtr = (uint32_t)value;
                st->initial_ndtr = (uint32_t)value;
                st->mem_index = 0U;
                if (stream_id == 1U) {
                    dma2_service_stream1(s);
                } else if (stream_id == 7U) {
                    dma2_service_stream7(s);
                }
                return;
            case DMA_SXPAR_OFF:
                st->par = (uint32_t)value;
                if (stream_id == 7U) {
                    dma2_service_stream7(s);
                }
                return;
            case DMA_SXM0AR_OFF:
                st->m0ar = (uint32_t)value;
                st->mem_index = 0U;
                if (stream_id == 1U) {
                    dma2_service_stream1(s);
                } else if (stream_id == 7U) {
                    dma2_service_stream7(s);
                }
                return;
            case DMA_SXM1AR_OFF:
                st->m1ar = (uint32_t)value;
                return;
            case DMA_SXFCR_OFF:
                st->fcr = (uint32_t)value;
                return;
            default:
                return;
            }
        }
    }
}
