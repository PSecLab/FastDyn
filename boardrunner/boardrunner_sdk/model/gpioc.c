#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define GPIOC_BASE_ADDR 0x40020800ULL

#define GPIO_MODER_OFF   0x00
#define GPIO_OTYPER_OFF  0x04
#define GPIO_OSPEEDR_OFF 0x08
#define GPIO_PUPDR_OFF   0x0C
#define GPIO_IDR_OFF     0x10
#define GPIO_ODR_OFF     0x14
#define GPIO_BSRR_OFF    0x18
#define GPIO_LCKR_OFF    0x1C
#define GPIO_AFRL_OFF    0x20
#define GPIO_AFRH_OFF    0x24

#define GPIOC_PC2_SIGNAL_ID  34
#define GPIOC_PC13_SIGNAL_ID 45
#define GPIOC_PC14_SIGNAL_ID 46
#define GPIOC_PC15_SIGNAL_ID 47

#define GPIOC_PC2_MASK  (1U << 2)
#define GPIOC_PC13_MASK (1U << 13)
#define GPIOC_PC14_MASK (1U << 14)
#define GPIOC_PC15_MASK (1U << 15)

typedef struct {
    uint32_t moder;
    uint32_t otyper;
    uint32_t ospeedr;
    uint32_t pupdr;
    uint32_t idr;
    uint32_t odr;
    uint32_t lckr;
    uint32_t afrl;
    uint32_t afrh;
} GPIOCState;

static GPIOCState g_gpioc_state;

static uint32_t gpioc_mask_for_size(unsigned size) {
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    default:
        return 0xFFFFFFFFU;
    }
}

static uint32_t gpioc_extract_reg32(uint32_t reg, uint64_t suboff, unsigned size) {
    uint32_t mask = gpioc_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);

    if (size >= 4) {
        return reg;
    }
    return (reg >> shift) & mask;
}

static uint32_t gpioc_merge_reg32(uint32_t oldv, uint64_t suboff, uint64_t value, unsigned size) {
    uint32_t mask = gpioc_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);
    uint32_t fullmask = (size >= 4) ? 0xFFFFFFFFU : (mask << shift);

    if (size >= 4) {
        return (uint32_t)value;
    }
    return (oldv & ~fullmask) | ((((uint32_t)value) & mask) << shift);
}

static uint32_t gpioc_subwrite_to_u32(uint64_t suboff, uint64_t value, unsigned size) {
    uint32_t mask = gpioc_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);

    if (size >= 4) {
        return (uint32_t)value;
    }
    return ((((uint32_t)value) & mask) << shift);
}

static void gpioc_publish_outputs(GPIOCState *s) {
    if (s == NULL) {
        return;
    }

    api_signal_set(GPIOC_PC2_SIGNAL_ID, (s->odr & GPIOC_PC2_MASK) != 0U);
    api_signal_set(GPIOC_PC13_SIGNAL_ID, (s->odr & GPIOC_PC13_MASK) != 0U);
    api_signal_set(GPIOC_PC14_SIGNAL_ID, (s->odr & GPIOC_PC14_MASK) != 0U);
    api_signal_set(GPIOC_PC15_SIGNAL_ID, (s->odr & GPIOC_PC15_MASK) != 0U);
}

static void gpioc_set_odr(GPIOCState *s, uint32_t odr) {
    if (s == NULL) {
        return;
    }

    s->odr = odr & 0xFFFFU;
    gpioc_publish_outputs(s);
}

static void gpioc_apply_bsrr(GPIOCState *s, uint32_t bsrr) {
    uint32_t set_bits = bsrr & 0xFFFFU;
    uint32_t rst_bits = (bsrr >> 16) & 0xFFFFU;
    uint32_t odr;

    if (s == NULL) {
        return;
    }

    odr = s->odr;
    odr |= set_bits;
    odr &= ~rst_bits;
    gpioc_set_odr(s, odr);
}

void* gpioc_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_gpioc_state, 0, sizeof(g_gpioc_state));
    return &g_gpioc_state;
}

uint64_t gpioc_read(void *opaque, uint64_t addr, unsigned size) {
    GPIOCState *s = (GPIOCState *)opaque;
    uint64_t offset = addr - GPIOC_BASE_ADDR;

    if (s == NULL) {
        s = &g_gpioc_state;
    }

    switch (offset & ~0x3ULL) {
    case GPIO_MODER_OFF:
        return gpioc_extract_reg32(s->moder, offset - GPIO_MODER_OFF, size);
    case GPIO_OTYPER_OFF:
        return gpioc_extract_reg32(s->otyper, offset - GPIO_OTYPER_OFF, size);
    case GPIO_OSPEEDR_OFF:
        return gpioc_extract_reg32(s->ospeedr, offset - GPIO_OSPEEDR_OFF, size);
    case GPIO_PUPDR_OFF:
        return gpioc_extract_reg32(s->pupdr, offset - GPIO_PUPDR_OFF, size);
    case GPIO_IDR_OFF:
        return gpioc_extract_reg32(s->idr, offset - GPIO_IDR_OFF, size);
    case GPIO_ODR_OFF:
        return gpioc_extract_reg32(s->odr, offset - GPIO_ODR_OFF, size);
    case GPIO_BSRR_OFF:
        return 0;
    case GPIO_LCKR_OFF:
        return gpioc_extract_reg32(s->lckr, offset - GPIO_LCKR_OFF, size);
    case GPIO_AFRL_OFF:
        return gpioc_extract_reg32(s->afrl, offset - GPIO_AFRL_OFF, size);
    case GPIO_AFRH_OFF:
        return gpioc_extract_reg32(s->afrh, offset - GPIO_AFRH_OFF, size);
    default:
        return 0;
    }
}

void gpioc_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    GPIOCState *s = (GPIOCState *)opaque;
    uint64_t offset = addr - GPIOC_BASE_ADDR;

    if (s == NULL) {
        s = &g_gpioc_state;
    }

    switch (offset & ~0x3ULL) {
    case GPIO_MODER_OFF:
        s->moder = gpioc_merge_reg32(s->moder, offset - GPIO_MODER_OFF, value, size);
        return;
    case GPIO_OTYPER_OFF:
        s->otyper = gpioc_merge_reg32(s->otyper, offset - GPIO_OTYPER_OFF, value, size) & 0xFFFFU;
        return;
    case GPIO_OSPEEDR_OFF:
        s->ospeedr = gpioc_merge_reg32(s->ospeedr, offset - GPIO_OSPEEDR_OFF, value, size);
        return;
    case GPIO_PUPDR_OFF:
        s->pupdr = gpioc_merge_reg32(s->pupdr, offset - GPIO_PUPDR_OFF, value, size);
        return;
    case GPIO_ODR_OFF:
        gpioc_set_odr(s, gpioc_merge_reg32(s->odr, offset - GPIO_ODR_OFF, value, size));
        return;
    case GPIO_BSRR_OFF: {
        uint32_t bsrr = gpioc_subwrite_to_u32(offset - GPIO_BSRR_OFF, value, size);
        gpioc_apply_bsrr(s, bsrr);
        return;
    }
    case GPIO_LCKR_OFF:
        s->lckr = gpioc_merge_reg32(s->lckr, offset - GPIO_LCKR_OFF, value, size);
        return;
    case GPIO_AFRL_OFF:
        s->afrl = gpioc_merge_reg32(s->afrl, offset - GPIO_AFRL_OFF, value, size);
        return;
    case GPIO_AFRH_OFF:
        s->afrh = gpioc_merge_reg32(s->afrh, offset - GPIO_AFRH_OFF, value, size);
        return;
    default:
        return;
    }
}
