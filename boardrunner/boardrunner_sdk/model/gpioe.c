#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define GPIOE_BASE_ADDR 0x40021000ULL

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

#define GPIOE_PE4_SIGNAL_ID 68
#define GPIOE_PE4_MASK      (1U << 4)

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
} GPIOEState;

static GPIOEState g_gpioe_state;

static uint32_t gpioe_mask_for_size(unsigned size) {
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    default:
        return 0xFFFFFFFFU;
    }
}

static uint32_t gpioe_extract_reg32(uint32_t reg, uint64_t suboff, unsigned size) {
    uint32_t mask = gpioe_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);

    if (size >= 4) {
        return reg;
    }
    return (reg >> shift) & mask;
}

static uint32_t gpioe_merge_reg32(uint32_t oldv, uint64_t suboff, uint64_t value, unsigned size) {
    uint32_t mask = gpioe_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);
    uint32_t fullmask = (size >= 4) ? 0xFFFFFFFFU : (mask << shift);

    if (size >= 4) {
        return (uint32_t)value;
    }
    return (oldv & ~fullmask) | ((((uint32_t)value) & mask) << shift);
}

static uint32_t gpioe_subwrite_to_u32(uint64_t suboff, uint64_t value, unsigned size) {
    uint32_t mask = gpioe_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);

    if (size >= 4) {
        return (uint32_t)value;
    }
    return ((((uint32_t)value) & mask) << shift);
}

static void gpioe_publish_outputs(GPIOEState *s) {
    bool pe4_level;

    if (s == NULL) {
        return;
    }

    if ((s->odr & GPIOE_PE4_MASK) != 0U) {
        s->idr |= GPIOE_PE4_MASK;
    } else {
        s->idr &= ~GPIOE_PE4_MASK;
    }

    pe4_level = (s->odr & GPIOE_PE4_MASK) != 0U;
    api_signal_set(GPIOE_PE4_SIGNAL_ID, pe4_level);
}

static void gpioe_apply_bsrr(GPIOEState *s, uint32_t bsrr) {
    uint32_t set_bits = bsrr & 0xFFFFU;
    uint32_t rst_bits = (bsrr >> 16) & 0xFFFFU;

    s->odr |= set_bits;
    s->odr &= ~rst_bits;
    s->odr &= 0xFFFFU;
    gpioe_publish_outputs(s);
}

void* gpioe_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_gpioe_state, 0, sizeof(g_gpioe_state));
    return &g_gpioe_state;
}

uint64_t gpioe_read(void *opaque, uint64_t addr, unsigned size) {
    GPIOEState *s = (GPIOEState *)opaque;
    uint64_t offset = addr - GPIOE_BASE_ADDR;

    if (s == NULL) {
        s = &g_gpioe_state;
    }

    switch (offset & ~0x3ULL) {
    case GPIO_MODER_OFF:
        return gpioe_extract_reg32(s->moder, offset - GPIO_MODER_OFF, size);
    case GPIO_OTYPER_OFF:
        return gpioe_extract_reg32(s->otyper, offset - GPIO_OTYPER_OFF, size);
    case GPIO_OSPEEDR_OFF:
        return gpioe_extract_reg32(s->ospeedr, offset - GPIO_OSPEEDR_OFF, size);
    case GPIO_PUPDR_OFF:
        return gpioe_extract_reg32(s->pupdr, offset - GPIO_PUPDR_OFF, size);
    case GPIO_IDR_OFF:
        return gpioe_extract_reg32(s->idr, offset - GPIO_IDR_OFF, size);
    case GPIO_ODR_OFF:
        return gpioe_extract_reg32(s->odr, offset - GPIO_ODR_OFF, size);
    case GPIO_BSRR_OFF:
        return 0;
    case GPIO_LCKR_OFF:
        return gpioe_extract_reg32(s->lckr, offset - GPIO_LCKR_OFF, size);
    case GPIO_AFRL_OFF:
        return gpioe_extract_reg32(s->afrl, offset - GPIO_AFRL_OFF, size);
    case GPIO_AFRH_OFF:
        return gpioe_extract_reg32(s->afrh, offset - GPIO_AFRH_OFF, size);
    default:
        return 0;
    }
}

void gpioe_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    GPIOEState *s = (GPIOEState *)opaque;
    uint64_t offset = addr - GPIOE_BASE_ADDR;

    if (s == NULL) {
        s = &g_gpioe_state;
    }

    switch (offset & ~0x3ULL) {
    case GPIO_MODER_OFF:
        s->moder = gpioe_merge_reg32(s->moder, offset - GPIO_MODER_OFF, value, size);
        return;
    case GPIO_OTYPER_OFF:
        s->otyper = gpioe_merge_reg32(s->otyper, offset - GPIO_OTYPER_OFF, value, size) & 0xFFFFU;
        return;
    case GPIO_OSPEEDR_OFF:
        s->ospeedr = gpioe_merge_reg32(s->ospeedr, offset - GPIO_OSPEEDR_OFF, value, size);
        return;
    case GPIO_PUPDR_OFF:
        s->pupdr = gpioe_merge_reg32(s->pupdr, offset - GPIO_PUPDR_OFF, value, size);
        return;
    case GPIO_ODR_OFF:
        s->odr = gpioe_merge_reg32(s->odr, offset - GPIO_ODR_OFF, value, size) & 0xFFFFU;
        gpioe_publish_outputs(s);
        return;
    case GPIO_BSRR_OFF: {
        uint32_t bsrr = gpioe_subwrite_to_u32(offset - GPIO_BSRR_OFF, value, size);
        gpioe_apply_bsrr(s, bsrr);
        return;
    }
    case GPIO_LCKR_OFF:
        s->lckr = gpioe_merge_reg32(s->lckr, offset - GPIO_LCKR_OFF, value, size);
        return;
    case GPIO_AFRL_OFF:
        s->afrl = gpioe_merge_reg32(s->afrl, offset - GPIO_AFRL_OFF, value, size);
        return;
    case GPIO_AFRH_OFF:
        s->afrh = gpioe_merge_reg32(s->afrh, offset - GPIO_AFRH_OFF, value, size);
        return;
    default:
        return;
    }
}
