#include <stdint.h>
#include <string.h>
#include <device.h>

#define OTG_FS_GLOBAL_BASE   0x50000000ULL
#define OTG_FS_GLOBAL_SIZE   0x400U

#define FS_GOTGCTL           0x000U
#define FS_GUSBCFG           0x00CU
#define FS_GRSTCTL           0x010U
#define FS_GCCFG             0x038U
#define FS_GSNPSID           0x03CU

#define GRSTCTL_CSRST        (1U << 0)
#define GRSTCTL_RXFFLSH      (1U << 4)
#define GRSTCTL_TXFFLSH      (1U << 5)
#define GRSTCTL_AHBIDL       (1U << 31)

/* Synopsys OTG core ID value commonly exposed by STM32 OTGv1 cores. */
#define GSNPSID_RESET_VALUE  0x4F54280AU

typedef struct {
    uint8_t regs[OTG_FS_GLOBAL_SIZE];
} OTGFSGlobalState;

static OTGFSGlobalState g_otg_fs_global;

static uint32_t otg_load32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void otg_store32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint32_t otg_reg_read32(OTGFSGlobalState *s, uint32_t offset) {
    return otg_load32(&s->regs[offset]);
}

static void otg_reg_write32(OTGFSGlobalState *s, uint32_t offset, uint32_t value) {
    otg_store32(&s->regs[offset], value);
}

static uint64_t otg_reg_read_sized(OTGFSGlobalState *s, uint32_t offset, unsigned size) {
    uint64_t value = 0;
    unsigned i;

    if ((size == 0U) || (size > 8U) || (offset + size > OTG_FS_GLOBAL_SIZE)) {
        return 0;
    }

    for (i = 0; i < size; i++) {
        value |= ((uint64_t)s->regs[offset + i]) << (8U * i);
    }

    return value;
}

static void otg_reg_write_sized(OTGFSGlobalState *s, uint32_t offset, uint64_t value, unsigned size) {
    unsigned i;

    if ((size == 0U) || (size > 8U) || (offset + size > OTG_FS_GLOBAL_SIZE)) {
        return;
    }

    for (i = 0; i < size; i++) {
        s->regs[offset + i] = (uint8_t)((value >> (8U * i)) & 0xFFU);
    }
}

static void otg_update_grstctl(OTGFSGlobalState *s) {
    uint32_t grstctl = otg_reg_read32(s, FS_GRSTCTL);

    /*
     * Minimal OTGv1 reset/flush behavior:
     * - AHB master is always idle in this simplified model.
     * - Reset/flush command bits complete immediately and self-clear.
     */
    grstctl |= GRSTCTL_AHBIDL;
    grstctl &= ~(GRSTCTL_CSRST | GRSTCTL_RXFFLSH | GRSTCTL_TXFFLSH);

    otg_reg_write32(s, FS_GRSTCTL, grstctl);
}

void* otg_fs_global_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_otg_fs_global, 0, sizeof(g_otg_fs_global));

    otg_reg_write32(&g_otg_fs_global, FS_GRSTCTL, GRSTCTL_AHBIDL);
    otg_reg_write32(&g_otg_fs_global, FS_GSNPSID, GSNPSID_RESET_VALUE);

    return &g_otg_fs_global;
}

uint64_t otg_fs_global_read(void *opaque, uint64_t addr, unsigned size) {
    OTGFSGlobalState *s = (OTGFSGlobalState *)opaque;
    uint32_t offset;

    if (s == NULL) {
        s = &g_otg_fs_global;
    }

    if ((addr < OTG_FS_GLOBAL_BASE) || (addr >= (OTG_FS_GLOBAL_BASE + OTG_FS_GLOBAL_SIZE))) {
        return 0;
    }

    offset = (uint32_t)(addr - OTG_FS_GLOBAL_BASE);

    if (offset == FS_GRSTCTL) {
        otg_update_grstctl(s);
    } else if (offset == FS_GSNPSID && size == 4U) {
        return GSNPSID_RESET_VALUE;
    }

    return otg_reg_read_sized(s, offset, size);
}

void otg_fs_global_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    OTGFSGlobalState *s = (OTGFSGlobalState *)opaque;
    uint32_t offset;

    if (s == NULL) {
        s = &g_otg_fs_global;
    }

    if ((addr < OTG_FS_GLOBAL_BASE) || (addr >= (OTG_FS_GLOBAL_BASE + OTG_FS_GLOBAL_SIZE))) {
        return;
    }

    offset = (uint32_t)(addr - OTG_FS_GLOBAL_BASE);

    /* Core ID register is read-only. */
    if ((offset == FS_GSNPSID) && (size == 4U)) {
        return;
    }

    otg_reg_write_sized(s, offset, value, size);

    /*
     * GRSTCTL contains command bits that hardware clears after completion.
     * ChibiOS polls AHBIDL first, then CSRST/RXFFLSH/TXFFLSH for deassertion.
     * Complete those commands immediately so the polling loops terminate.
     */
    if ((offset <= FS_GRSTCTL) && ((offset + size) > FS_GRSTCTL)) {
        otg_update_grstctl(s);
    }
}
