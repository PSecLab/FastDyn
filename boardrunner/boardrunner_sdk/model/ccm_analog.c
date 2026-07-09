#include <stdint.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define CCM_ANALOG_BASE_ADDR 0x400D8000ULL
#define CCM_ANALOG_SIZE      0x184U

/* Base register offsets. SET/CLR/TOG aliases live at +0x4/+0x8/+0xC. */
#define PLL_ARM_OFF          0x000U
#define PLL_USB1_OFF         0x010U
#define PLL_USB2_OFF         0x020U
#define PLL_SYS_OFF          0x030U
#define PLL_AUDIO_OFF        0x070U
#define PLL_VIDEO_OFF        0x0A0U
#define PLL_ENET_OFF         0x0E0U

#define PLL_ARM_ENABLE_MASK          0x00002000U
#define PLL_ARM_POWERDOWN_MASK       0x00001000U
#define PLL_ARM_LOCK_MASK            0x80000000U

#define PLL_SYS_ENABLE_MASK          0x00002000U
#define PLL_SYS_POWERDOWN_MASK       0x00001000U
#define PLL_SYS_LOCK_MASK            0x80000000U

/* USB PLLs use POWER rather than POWERDOWN at bit 12. */
#define PLL_USB_ENABLE_MASK          0x00002000U
#define PLL_USB_POWER_MASK           0x00001000U
#define PLL_USB_LOCK_MASK            0x80000000U

#define PLL_AUDIO_ENABLE_MASK        0x00002000U
#define PLL_AUDIO_POWERDOWN_MASK     0x00001000U
#define PLL_AUDIO_LOCK_MASK          0x80000000U

#define PLL_VIDEO_ENABLE_MASK        0x00002000U
#define PLL_VIDEO_POWERDOWN_MASK     0x00001000U
#define PLL_VIDEO_LOCK_MASK          0x80000000U

#define PLL_ENET_ENABLE_MASK         0x00002000U
#define PLL_ENET_POWERDOWN_MASK      0x00001000U
#define PLL_ENET_LOCK_MASK           0x80000000U
#define PLL_ENET_ENET2_REF_EN_MASK   0x00100000U
#define PLL_ENET_25M_REF_EN_MASK     0x00200000U

#define REG_WORDS ((CCM_ANALOG_SIZE + 3U) / 4U)

typedef struct {
    uint32_t regs[REG_WORDS];
} CCMAnalogState;

static CCMAnalogState g_ccm_analog;

static uint32_t size_mask(unsigned size) {
    if (size >= 4U) {
        return 0xFFFFFFFFU;
    }
    return (uint32_t)((1ULL << (size * 8U)) - 1ULL);
}

static int offset_valid(uint64_t offset, unsigned size) {
    if (size != 1U && size != 2U && size != 4U) {
        return 0;
    }
    if (offset >= CCM_ANALOG_SIZE) {
        return 0;
    }
    if ((offset + size) > CCM_ANALOG_SIZE) {
        return 0;
    }
    return 1;
}

static uint32_t canonical_offset(uint32_t aligned_off) {
    switch (aligned_off & 0xFU) {
        case 0x4U:
        case 0x8U:
        case 0xCU:
            return aligned_off & ~0xFU;
        default:
            return aligned_off;
    }
}

static uint32_t apply_pll_status(uint32_t canon_off, uint32_t value) {
    switch (canon_off) {
        case PLL_ARM_OFF:
            if ((value & PLL_ARM_ENABLE_MASK) && ((value & PLL_ARM_POWERDOWN_MASK) == 0U)) {
                value |= PLL_ARM_LOCK_MASK;
            } else {
                value &= ~PLL_ARM_LOCK_MASK;
            }
            break;

        case PLL_SYS_OFF:
            if ((value & PLL_SYS_ENABLE_MASK) && ((value & PLL_SYS_POWERDOWN_MASK) == 0U)) {
                value |= PLL_SYS_LOCK_MASK;
            } else {
                value &= ~PLL_SYS_LOCK_MASK;
            }
            break;

        case PLL_USB1_OFF:
        case PLL_USB2_OFF:
            if ((value & PLL_USB_ENABLE_MASK) && (value & PLL_USB_POWER_MASK)) {
                value |= PLL_USB_LOCK_MASK;
            } else {
                value &= ~PLL_USB_LOCK_MASK;
            }
            break;

        case PLL_AUDIO_OFF:
            if ((value & PLL_AUDIO_ENABLE_MASK) && ((value & PLL_AUDIO_POWERDOWN_MASK) == 0U)) {
                value |= PLL_AUDIO_LOCK_MASK;
            } else {
                value &= ~PLL_AUDIO_LOCK_MASK;
            }
            break;

        case PLL_VIDEO_OFF:
            if ((value & PLL_VIDEO_ENABLE_MASK) && ((value & PLL_VIDEO_POWERDOWN_MASK) == 0U)) {
                value |= PLL_VIDEO_LOCK_MASK;
            } else {
                value &= ~PLL_VIDEO_LOCK_MASK;
            }
            break;

        case PLL_ENET_OFF:
            if (((value & PLL_ENET_ENABLE_MASK) ||
                 (value & PLL_ENET_ENET2_REF_EN_MASK) ||
                 (value & PLL_ENET_25M_REF_EN_MASK)) &&
                ((value & PLL_ENET_POWERDOWN_MASK) == 0U)) {
                value |= PLL_ENET_LOCK_MASK;
            } else {
                value &= ~PLL_ENET_LOCK_MASK;
            }
            break;

        default:
            break;
    }

    return value;
}

static void normalize_special(CCMAnalogState *s, uint32_t canon_off) {
    if ((canon_off >> 2) >= REG_WORDS) {
        return;
    }

    s->regs[canon_off >> 2] = apply_pll_status(canon_off, s->regs[canon_off >> 2]);
}

static uint32_t reg_read32(CCMAnalogState *s, uint32_t aligned_off) {
    uint32_t canon = canonical_offset(aligned_off);

    if ((canon >> 2) >= REG_WORDS) {
        return 0U;
    }

    normalize_special(s, canon);
    return s->regs[canon >> 2];
}

static void reg_write32(CCMAnalogState *s, uint32_t aligned_off, uint32_t value, unsigned size) {
    uint32_t canon = canonical_offset(aligned_off);
    uint32_t shift = (aligned_off & 0x3U) * 8U;
    uint32_t mask = size_mask(size) << shift;
    uint32_t data = (value & size_mask(size)) << shift;
    uint32_t cur;

    if ((canon >> 2) >= REG_WORDS) {
        return;
    }

    cur = s->regs[canon >> 2];

    switch (aligned_off & 0xFU) {
        case 0x4U:
            cur |= data;
            break;
        case 0x8U:
            cur &= ~data;
            break;
        case 0xCU:
            cur ^= data;
            break;
        default:
            cur = (cur & ~mask) | data;
            break;
    }

    s->regs[canon >> 2] = apply_pll_status(canon, cur);
}

void* ccm_analog_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_ccm_analog, 0, sizeof(g_ccm_analog));
    return &g_ccm_analog;
}

uint64_t ccm_analog_read(void *opaque, uint64_t addr, unsigned size) {
    CCMAnalogState *s = (CCMAnalogState *)opaque;
    uint64_t offset;
    uint32_t aligned_off;
    uint32_t word;
    uint32_t shift;

    if (s == NULL) {
        s = &g_ccm_analog;
    }

    if (addr < CCM_ANALOG_BASE_ADDR) {
        return 0;
    }

    offset = addr - CCM_ANALOG_BASE_ADDR;
    if (!offset_valid(offset, size)) {
        return 0;
    }

    aligned_off = (uint32_t)(offset & ~0x3ULL);
    shift = (uint32_t)(offset & 0x3ULL) * 8U;
    word = reg_read32(s, aligned_off);

    return (word >> shift) & size_mask(size);
}

void ccm_analog_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    CCMAnalogState *s = (CCMAnalogState *)opaque;
    uint64_t offset;
    uint32_t aligned_off;

    if (s == NULL) {
        s = &g_ccm_analog;
    }

    if (addr < CCM_ANALOG_BASE_ADDR) {
        return;
    }

    offset = addr - CCM_ANALOG_BASE_ADDR;
    if (!offset_valid(offset, size)) {
        return;
    }

    aligned_off = (uint32_t)(offset & ~0x3ULL);
    reg_write32(s, aligned_off, (uint32_t)value, size);
}
