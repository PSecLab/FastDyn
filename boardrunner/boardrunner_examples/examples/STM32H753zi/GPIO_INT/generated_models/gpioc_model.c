// Device Model for GPIOC
//
// Inferred Register Functions:
//   0x00 MODER   - GPIO port mode register
//   0x04 OTYPER  - GPIO port output type register
//   0x08 OSPEEDR - GPIO port output speed register
//   0x0C PUPDR   - GPIO port pull-up/pull-down register
//   0x10 IDR     - GPIO port input data register
//   0x14 ODR     - GPIO port output data register
//   0x18 BSRR    - GPIO port bit set/reset register
//   0x1C LCKR    - GPIO port configuration lock register
//   0x20 AFRL    - GPIO alternate function low register
//   0x24 AFRH    - GPIO alternate function high register
//   0x28 BRR     - GPIO port bit reset register
//
// Trace evidence specifically confirms stateful behavior for MODER and PUPDR.
// MODER reset value is modeled as 0xFFFFFFFF because that is what firmware read.
// PC13 is exposed as a host-driven external input through a FIFO.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define GPIOC_BASE       0x58020800ULL

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
#define GPIO_BRR_OFF     0x28

#define GPIOC_FIFO_PATH  "/tmp/gpioc_pc13"

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

    /* External input levels provided by host; only low 16 bits used. */
    uint32_t ext_inputs;

    int fifo_fd;
    uint64_t poll_timer;
} GPIOCState;

static GPIOCState gpioc_state;

static GPIOCState *gpioc_get_state(void *opaque)
{
    if (opaque) {
        return (GPIOCState *)opaque;
    }
    return &gpioc_state;
}

static uint32_t gpioc_mask_for_size(unsigned size)
{
    switch (size) {
    case 1:
        return 0xFFu;
    case 2:
        return 0xFFFFu;
    case 4:
    default:
        return 0xFFFFFFFFu;
    }
}

static uint32_t gpioc_extract_reg(uint32_t reg, hwaddr offset, unsigned size)
{
    unsigned shift = (unsigned)(offset & 0x3u) * 8u;
    uint32_t mask = gpioc_mask_for_size(size);
    return (reg >> shift) & mask;
}

static uint32_t gpioc_deposit_reg(uint32_t oldval, hwaddr offset, uint64_t value, unsigned size)
{
    unsigned shift = (unsigned)(offset & 0x3u) * 8u;
    uint32_t mask = gpioc_mask_for_size(size) << shift;
    uint32_t v = ((uint32_t)value << shift) & mask;
    return (oldval & ~mask) | v;
}

static uint32_t gpioc_expand_write(hwaddr offset, uint64_t value, unsigned size)
{
    unsigned shift = (unsigned)(offset & 0x3u) * 8u;
    return ((uint32_t)value) << shift;
}

static void gpioc_refresh_idr(GPIOCState *s)
{
    uint32_t idr = 0;
    int pin;

    for (pin = 0; pin < 16; pin++) {
        uint32_t mode = (s->moder >> (pin * 2)) & 0x3u;
        uint32_t bit = 1u << pin;

        /*
         * Minimal model:
         * - Output mode (01): pin level follows ODR.
         * - Any non-output mode: pin level follows external input.
         */
        if (mode == 0x1u) {
            if (s->odr & bit) {
                idr |= bit;
            }
        } else {
            if (s->ext_inputs & bit) {
                idr |= bit;
            }
        }
    }

    s->idr = idr & 0xFFFFu;

    /*
     * Publish PC13 level to EXTI line 13. Edge detection belongs in EXTI;
     * GPIO provides the current logic level.
     */
    api_signal_set(13, ((s->idr >> 13) & 0x1u) != 0);
}

static void gpioc_fifo_poll(void *opaque)
{
    GPIOCState *s = (GPIOCState *)opaque;
    uint8_t ch;
    char msg[64];

    if (s->fifo_fd < 0) {
        return;
    }

    while (api_fifo_read_nonblock(s->fifo_fd, &ch) == 1) {
        if (ch == '0') {
            s->ext_inputs &= ~(1u << 13);
            gpioc_refresh_idr(s);
            snprintf(msg, sizeof(msg), "GPIOC: PC13 input level set to 0\n");
            dev_debug(msg);
        } else if (ch == '1') {
            s->ext_inputs |= (1u << 13);
            gpioc_refresh_idr(s);
            snprintf(msg, sizeof(msg), "GPIOC: PC13 input level set to 1\n");
            dev_debug(msg);
        } else {
            /* Ignore whitespace or any unsupported control byte. */
        }
    }
}

static void gpioc_log_unknown_access(const char *kind, hwaddr addr)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "GPIOC: unknown %s at 0x%llx\n",
             kind, (unsigned long long)addr);
    dev_debug(msg);
}

// This function will emulation all device reads
uint64_t gpioc_read(void *opaque, hwaddr addr, unsigned size)
{
    GPIOCState *s = gpioc_get_state(opaque);
    hwaddr offset;
    hwaddr regoff;

    if (addr < GPIOC_BASE) {
        gpioc_log_unknown_access("read", addr);
        return 0;
    }

    offset = addr - GPIOC_BASE;
    regoff = offset & ~0x3ULL;

    switch (regoff) {
    case GPIO_MODER_OFF:
        return gpioc_extract_reg(s->moder, offset, size);

    case GPIO_OTYPER_OFF:
        return gpioc_extract_reg(s->otyper, offset, size);

    case GPIO_OSPEEDR_OFF:
        return gpioc_extract_reg(s->ospeedr, offset, size);

    case GPIO_PUPDR_OFF:
        return gpioc_extract_reg(s->pupdr, offset, size);

    case GPIO_IDR_OFF:
        gpioc_refresh_idr(s);
        return gpioc_extract_reg(s->idr, offset, size);

    case GPIO_ODR_OFF:
        return gpioc_extract_reg(s->odr, offset, size);

    case GPIO_BSRR_OFF:
        /* Write-only in hardware; return 0 in this minimal model. */
        return 0;

    case GPIO_LCKR_OFF:
        return gpioc_extract_reg(s->lckr, offset, size);

    case GPIO_AFRL_OFF:
        return gpioc_extract_reg(s->afrl, offset, size);

    case GPIO_AFRH_OFF:
        return gpioc_extract_reg(s->afrh, offset, size);

    case GPIO_BRR_OFF:
        /* Write-only in hardware; return 0 in this minimal model. */
        return 0;

    default:
        gpioc_log_unknown_access("read", addr);
        return 0;
    }
}

// This function will emulate all device writes
void gpioc_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    GPIOCState *s = gpioc_get_state(opaque);
    hwaddr offset;
    hwaddr regoff;
    uint32_t v32;

    if (addr < GPIOC_BASE) {
        gpioc_log_unknown_access("write", addr);
        return;
    }

    offset = addr - GPIOC_BASE;
    regoff = offset & ~0x3ULL;

    switch (regoff) {
    case GPIO_MODER_OFF:
        s->moder = gpioc_deposit_reg(s->moder, offset, value, size);
        gpioc_refresh_idr(s);
        break;

    case GPIO_OTYPER_OFF:
        s->otyper = gpioc_deposit_reg(s->otyper, offset, value, size) & 0x0000FFFFu;
        break;

    case GPIO_OSPEEDR_OFF:
        s->ospeedr = gpioc_deposit_reg(s->ospeedr, offset, value, size);
        break;

    case GPIO_PUPDR_OFF:
        s->pupdr = gpioc_deposit_reg(s->pupdr, offset, value, size);
        break;

    case GPIO_IDR_OFF:
        /* Read-only register; ignore writes. */
        break;

    case GPIO_ODR_OFF:
        s->odr = gpioc_deposit_reg(s->odr, offset, value, size) & 0x0000FFFFu;
        gpioc_refresh_idr(s);
        break;

    case GPIO_BSRR_OFF:
        v32 = gpioc_expand_write(offset, value, size);
        s->odr |= (v32 & 0x0000FFFFu);
        s->odr &= ~((v32 >> 16) & 0x0000FFFFu);
        s->odr &= 0x0000FFFFu;
        gpioc_refresh_idr(s);
        break;

    case GPIO_LCKR_OFF:
        s->lckr = gpioc_deposit_reg(s->lckr, offset, value, size) & 0x0001FFFFu;
        break;

    case GPIO_AFRL_OFF:
        s->afrl = gpioc_deposit_reg(s->afrl, offset, value, size);
        break;

    case GPIO_AFRH_OFF:
        s->afrh = gpioc_deposit_reg(s->afrh, offset, value, size);
        break;

    case GPIO_BRR_OFF:
        v32 = gpioc_expand_write(offset, value, size);
        s->odr &= ~(v32 & 0x0000FFFFu);
        s->odr &= 0x0000FFFFu;
        gpioc_refresh_idr(s);
        break;

    default:
        gpioc_log_unknown_access("write", addr);
        break;
    }
}

void gpioc_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&gpioc_state, 0, sizeof(gpioc_state));

    /*
     * Trace-derived reset state:
     * - MODER read as 0xFFFFFFFF before firmware reconfiguration.
     * - PUPDR read as 0x00000000.
     * Other registers default to 0 for this minimal model.
     */
    gpioc_state.moder = 0xFFFFFFFFu;
    gpioc_state.pupdr = 0x00000000u;
    gpioc_state.ext_inputs = 0x00000000u; /* Host can drive PC13 via FIFO. */
    gpioc_refresh_idr(&gpioc_state);

    gpioc_state.fifo_fd = api_fifo_open(GPIOC_FIFO_PATH);
    gpioc_state.poll_timer = qemu_plugin_timer_new_period_ns(gpioc_fifo_poll,
                                                             &gpioc_state,
                                                             1000000ULL);

    dev_debug("GPIOC: initialized, PC13 host input FIFO at /tmp/gpioc_pc13\n");
}