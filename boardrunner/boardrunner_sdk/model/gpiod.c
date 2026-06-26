#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>

#define GPIOD_BASE_ADDR 0x40020C00ULL

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

#define GPIOD_PD10_SIGNAL_ID 58
#define GPIOD_PD10_MASK      (1U << 10)

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
} GPIODState;

static GPIODState g_gpiod_state;

static uint32_t gpiod_mask_for_size(unsigned size) {
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    default:
        return 0xFFFFFFFFU;
    }
}

static uint32_t gpiod_extract_reg32(uint32_t reg, uint64_t suboff, unsigned size) {
    uint32_t mask = gpiod_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);

    if (size >= 4) {
        return reg;
    }
    return (reg >> shift) & mask;
}

static uint32_t gpiod_merge_reg32(uint32_t oldv, uint64_t suboff, uint64_t value, unsigned size) {
    uint32_t mask = gpiod_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);
    uint32_t fullmask = (size >= 4) ? 0xFFFFFFFFU : (mask << shift);

    if (size >= 4) {
        return (uint32_t)value;
    }
    return (oldv & ~fullmask) | ((((uint32_t)value) & mask) << shift);
}

static uint32_t gpiod_subwrite_to_u32(uint64_t suboff, uint64_t value, unsigned size) {
    uint32_t mask = gpiod_mask_for_size(size);
    unsigned shift = (unsigned)(suboff * 8U);

    if (size >= 4) {
        return (uint32_t)value;
    }
    return (((uint32_t)value) & mask) << shift;
}

static void gpiod_publish_pd10(GPIODState *s) {
    bool level;

    if (s == NULL) {
        return;
    }

    if ((s->odr & GPIOD_PD10_MASK) != 0) {
        s->idr |= GPIOD_PD10_MASK;
    } else {
        s->idr &= ~GPIOD_PD10_MASK;
    }

    level = (s->odr & GPIOD_PD10_MASK) != 0;
    api_signal_set(GPIOD_PD10_SIGNAL_ID, level);
}

static void gpiod_apply_bsrr(GPIODState *s, uint32_t bsrr) {
    uint32_t set_bits = bsrr & 0xFFFFU;
    uint32_t rst_bits = (bsrr >> 16) & 0xFFFFU;

    s->odr |= set_bits;
    s->odr &= ~rst_bits;
    s->odr &= 0xFFFFU;
    gpiod_publish_pd10(s);
}

void* gpiod_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_gpiod_state, 0, sizeof(g_gpiod_state));
    return &g_gpiod_state;
}

uint64_t gpiod_read(void *opaque, uint64_t addr, unsigned size) {
    GPIODState *s = (GPIODState *)opaque;
    uint64_t offset = addr - GPIOD_BASE_ADDR;

    if (s == NULL) {
        s = &g_gpiod_state;
    }

    switch (offset & ~0x3ULL) {
    case GPIO_MODER_OFF:
        return gpiod_extract_reg32(s->moder, offset - GPIO_MODER_OFF, size);
    case GPIO_OTYPER_OFF:
        return gpiod_extract_reg32(s->otyper, offset - GPIO_OTYPER_OFF, size);
    case GPIO_OSPEEDR_OFF:
        return gpiod_extract_reg32(s->ospeedr, offset - GPIO_OSPEEDR_OFF, size);
    case GPIO_PUPDR_OFF:
        return gpiod_extract_reg32(s->pupdr, offset - GPIO_PUPDR_OFF, size);
    case GPIO_IDR_OFF:
        return gpiod_extract_reg32(s->idr, offset - GPIO_IDR_OFF, size);
    case GPIO_ODR_OFF:
        return gpiod_extract_reg32(s->odr, offset - GPIO_ODR_OFF, size);
    case GPIO_BSRR_OFF:
        return 0;
    case GPIO_LCKR_OFF:
        return gpiod_extract_reg32(s->lckr, offset - GPIO_LCKR_OFF, size);
    case GPIO_AFRL_OFF:
        return gpiod_extract_reg32(s->afrl, offset - GPIO_AFRL_OFF, size);
    case GPIO_AFRH_OFF:
        return gpiod_extract_reg32(s->afrh, offset - GPIO_AFRH_OFF, size);
    default:
        return 0;
    }
}

void gpiod_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    GPIODState *s = (GPIODState *)opaque;
    uint64_t offset = addr - GPIOD_BASE_ADDR;

    if (s == NULL) {
        s = &g_gpiod_state;
    }

    switch (offset & ~0x3ULL) {
    case GPIO_MODER_OFF:
        s->moder = gpiod_merge_reg32(s->moder, offset - GPIO_MODER_OFF, value, size);
        return;
    case GPIO_OTYPER_OFF:
        s->otyper = gpiod_merge_reg32(s->otyper, offset - GPIO_OTYPER_OFF, value, size) & 0xFFFFU;
        return;
    case GPIO_OSPEEDR_OFF:
        s->ospeedr = gpiod_merge_reg32(s->ospeedr, offset - GPIO_OSPEEDR_OFF, value, size);
        return;
    case GPIO_PUPDR_OFF:
        s->pupdr = gpiod_merge_reg32(s->pupdr, offset - GPIO_PUPDR_OFF, value, size);
        return;
    case GPIO_ODR_OFF:
        s->odr = gpiod_merge_reg32(s->odr, offset - GPIO_ODR_OFF, value, size) & 0xFFFFU;
        gpiod_publish_pd10(s);
        return;
    case GPIO_BSRR_OFF: {
        uint32_t bsrr = gpiod_subwrite_to_u32(offset - GPIO_BSRR_OFF, value, size);
        gpiod_apply_bsrr(s, bsrr);
        return;
    }
    case GPIO_LCKR_OFF:
        s->lckr = gpiod_merge_reg32(s->lckr, offset - GPIO_LCKR_OFF, value, size);
        return;
    case GPIO_AFRL_OFF:
        s->afrl = gpiod_merge_reg32(s->afrl, offset - GPIO_AFRL_OFF, value, size);
        return;
    case GPIO_AFRH_OFF:
        s->afrh = gpiod_merge_reg32(s->afrh, offset - GPIO_AFRH_OFF, value, size);
        return;
    default:
        return;
    }
}