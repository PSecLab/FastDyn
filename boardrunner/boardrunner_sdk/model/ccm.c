#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define CCM_BASE        0x400FC000ULL
#define CCM_WINDOW_SIZE 0x90U
#define CCM_REG_WORDS   (CCM_WINDOW_SIZE / 4U)

#define CCM_CCSR    0x0CU
#define CCM_CACRR   0x10U
#define CCM_CBCDR   0x14U
#define CCM_CBCMR   0x18U
#define CCM_CSCMR1  0x1CU
#define CCM_CSCMR2  0x20U
#define CCM_CSCDR1  0x24U
#define CCM_CS1CDR  0x28U
#define CCM_CS2CDR  0x2CU
#define CCM_CDCDR   0x30U
#define CCM_CSCDR2  0x38U
#define CCM_CSCDR3  0x3CU
#define CCM_CDHIPR  0x48U
#define CCM_CLPCR   0x54U
#define CCM_CCGR0   0x68U
#define CCM_CCGR1   0x6CU
#define CCM_CCGR2   0x70U
#define CCM_CCGR3   0x74U
#define CCM_CCGR4   0x78U
#define CCM_CCGR5   0x7CU
#define CCM_CCGR6   0x80U
#define CCM_CCGR7   0x84U

typedef struct {
    uint32_t regs[CCM_REG_WORDS];
} CCMState;

static CCMState g_ccm;

static uint32_t ccm_size_mask(unsigned size, unsigned shift)
{
    if (size >= 4U) {
        return 0xFFFFFFFFU;
    }

    return ((1U << (size * 8U)) - 1U) << shift;
}

static uint32_t ccm_read_word(CCMState *s, uint32_t aligned_offset)
{
    if (aligned_offset >= CCM_WINDOW_SIZE) {
        return 0;
    }

    switch (aligned_offset) {
    case CCM_CDHIPR:
        return 0U;
    default:
        return s->regs[aligned_offset >> 2];
    }
}

static void ccm_write_word(CCMState *s, uint32_t aligned_offset, uint32_t value)
{
    if (aligned_offset >= CCM_WINDOW_SIZE) {
        return;
    }

    switch (aligned_offset) {
    case CCM_CDHIPR:
        return;
    default:
        s->regs[aligned_offset >> 2] = value;
        return;
    }
}

void* ccm_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_ccm, 0, sizeof(g_ccm));

    /* Keep the default PLL3 SW clock select at 0 so PLL3 is chosen if queried. */
    g_ccm.regs[CCM_CCSR >> 2] = 0U;

    return &g_ccm;
}

uint64_t ccm_read(void *opaque, uint64_t addr, unsigned size)
{
    CCMState *s = (CCMState *)opaque;
    uint32_t offset;
    uint32_t aligned_offset;
    uint32_t shift;
    uint32_t word;

    if (s == NULL) {
        s = &g_ccm;
    }

    if (addr < CCM_BASE || addr >= (CCM_BASE + CCM_WINDOW_SIZE)) {
        return 0;
    }

    offset = (uint32_t)(addr - CCM_BASE);
    aligned_offset = offset & ~0x3U;
    shift = (offset & 0x3U) * 8U;
    word = ccm_read_word(s, aligned_offset);

    if (size >= 4U) {
        return word;
    }

    return (word >> shift) & ((1ULL << (size * 8U)) - 1ULL);
}

void ccm_write(void *opaque, uint64_t addr, uint64_t value, unsigned size)
{
    CCMState *s = (CCMState *)opaque;
    uint32_t offset;
    uint32_t aligned_offset;
    uint32_t shift;
    uint32_t current;
    uint32_t merged;
    uint32_t mask;

    if (s == NULL) {
        s = &g_ccm;
    }

    if (addr < CCM_BASE || addr >= (CCM_BASE + CCM_WINDOW_SIZE)) {
        return;
    }

    offset = (uint32_t)(addr - CCM_BASE);
    aligned_offset = offset & ~0x3U;

    if (size >= 4U) {
        ccm_write_word(s, aligned_offset, (uint32_t)value);
        return;
    }

    shift = (offset & 0x3U) * 8U;
    current = ccm_read_word(s, aligned_offset);
    mask = ccm_size_mask(size, shift);
    merged = (current & ~mask) | ((((uint32_t)value) << shift) & mask);

    ccm_write_word(s, aligned_offset, merged);
}
