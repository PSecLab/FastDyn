#include <stdint.h>
#include <string.h>
#include <device.h>

#define SDIO_BASE_ADDR          0x40012C00ULL

#define SDIO_POWER_OFF          0x00
#define SDIO_CLKCR_OFF          0x04
#define SDIO_ARG_OFF            0x08
#define SDIO_CMD_OFF            0x0C
#define SDIO_RESPCMD_OFF        0x10
#define SDIO_RESP1_OFF          0x14
#define SDIO_RESP2_OFF          0x18
#define SDIO_RESP3_OFF          0x1C
#define SDIO_RESP4_OFF          0x20
#define SDIO_DTIMER_OFF         0x24
#define SDIO_DLEN_OFF           0x28
#define SDIO_DCTRL_OFF          0x2C
#define SDIO_DCOUNT_OFF         0x30
#define SDIO_STA_OFF            0x34
#define SDIO_ICR_OFF            0x38
#define SDIO_MASK_OFF           0x3C
#define SDIO_FIFOCNT_OFF        0x48
#define SDIO_FIFO_OFF           0x80
#define SDIO_FIFO_END           0xBC

#define SDIO_POWER_PWRCTRL_MASK 0x00000003U

#define SDIO_CMD_WAITRESP_MASK  0x000000C0U
#define SDIO_CMD_CPSMEN         0x00000400U

#define SDIO_STA_CCRCFAIL       (1U << 0)
#define SDIO_STA_CTIMEOUT       (1U << 2)
#define SDIO_STA_CMDREND        (1U << 6)
#define SDIO_STA_CMDSENT        (1U << 7)

typedef struct {
    uint32_t power;
    uint32_t clkcr;
    uint32_t arg;
    uint32_t cmd;
    uint32_t respcmd;
    uint32_t resp1;
    uint32_t resp2;
    uint32_t resp3;
    uint32_t resp4;
    uint32_t dtimer;
    uint32_t dlen;
    uint32_t dctrl;
    uint32_t dcount;
    uint32_t sta;
    uint32_t mask;
    uint32_t fifocnt;
} SDIOState;

static SDIOState g_sdio;

static uint32_t sdio_access_mask(uint64_t addr, unsigned size) {
    unsigned shift = (unsigned)(addr & 0x3ULL) * 8U;

    if (size == 1) {
        return 0xFFU << shift;
    }
    if (size == 2) {
        return 0xFFFFU << shift;
    }
    return 0xFFFFFFFFU;
}

static uint32_t sdio_write_merge(uint32_t oldv, uint64_t addr, uint64_t value, unsigned size) {
    uint32_t mask = sdio_access_mask(addr, size);
    unsigned shift = (unsigned)(addr & 0x3ULL) * 8U;
    uint32_t newbits = ((uint32_t)value << shift) & mask;
    return (oldv & ~mask) | newbits;
}

static uint64_t sdio_read_extract(uint32_t regv, uint64_t addr, unsigned size) {
    unsigned shift = (unsigned)(addr & 0x3ULL) * 8U;

    if (size == 1) {
        return (regv >> shift) & 0xFFU;
    }
    if (size == 2) {
        return (regv >> shift) & 0xFFFFU;
    }
    return regv;
}

static void sdio_start_command(SDIOState *s, uint32_t cmd_value) {
    uint32_t waitresp;

    s->cmd = cmd_value;
    s->respcmd = cmd_value & 0x3FU;

    s->sta &= ~(SDIO_STA_CCRCFAIL |
                SDIO_STA_CTIMEOUT |
                SDIO_STA_CMDREND |
                SDIO_STA_CMDSENT);

    s->resp1 = 0;
    s->resp2 = 0;
    s->resp3 = 0;
    s->resp4 = 0;

    if ((cmd_value & SDIO_CMD_CPSMEN) == 0U) {
        return;
    }

    waitresp = cmd_value & SDIO_CMD_WAITRESP_MASK;

    /*
     * Minimal but stateful behavior:
     * - no-response commands complete immediately with CMDSENT
     * - response commands complete immediately with CTIMEOUT, modeling
     *   an absent/unimplemented card while still allowing firmware to
     *   escape polling loops.
     */
    if (waitresp == 0U) {
        s->sta |= SDIO_STA_CMDSENT;
    } else {
        s->sta |= SDIO_STA_CTIMEOUT;
    }
}

void* sdio_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_sdio, 0, sizeof(g_sdio));
    return &g_sdio;
}

uint64_t sdio_read(void *opaque, uint64_t addr, unsigned size) {
    SDIOState *s = (SDIOState *)opaque;
    uint64_t offset;

    if (s == NULL) {
        s = &g_sdio;
    }

    if (addr < SDIO_BASE_ADDR) {
        return 0;
    }

    offset = addr - SDIO_BASE_ADDR;

    switch (offset) {
        case SDIO_POWER_OFF:
            return sdio_read_extract(s->power, addr, size);
        case SDIO_CLKCR_OFF:
            return sdio_read_extract(s->clkcr, addr, size);
        case SDIO_ARG_OFF:
            return sdio_read_extract(s->arg, addr, size);
        case SDIO_CMD_OFF:
            return sdio_read_extract(s->cmd, addr, size);
        case SDIO_RESPCMD_OFF:
            return sdio_read_extract(s->respcmd, addr, size);
        case SDIO_RESP1_OFF:
            return sdio_read_extract(s->resp1, addr, size);
        case SDIO_RESP2_OFF:
            return sdio_read_extract(s->resp2, addr, size);
        case SDIO_RESP3_OFF:
            return sdio_read_extract(s->resp3, addr, size);
        case SDIO_RESP4_OFF:
            return sdio_read_extract(s->resp4, addr, size);
        case SDIO_DTIMER_OFF:
            return sdio_read_extract(s->dtimer, addr, size);
        case SDIO_DLEN_OFF:
            return sdio_read_extract(s->dlen, addr, size);
        case SDIO_DCTRL_OFF:
            return sdio_read_extract(s->dctrl, addr, size);
        case SDIO_DCOUNT_OFF:
            return sdio_read_extract(s->dcount, addr, size);
        case SDIO_STA_OFF:
            return sdio_read_extract(s->sta, addr, size);
        case SDIO_MASK_OFF:
            return sdio_read_extract(s->mask, addr, size);
        case SDIO_FIFOCNT_OFF:
            return sdio_read_extract(s->fifocnt, addr, size);
        default:
            if ((offset >= SDIO_FIFO_OFF) && (offset <= SDIO_FIFO_END)) {
                return 0;
            }
            return 0;
    }
}

void sdio_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    SDIOState *s = (SDIOState *)opaque;
    uint64_t offset;

    if (s == NULL) {
        s = &g_sdio;
    }

    if (addr < SDIO_BASE_ADDR) {
        return;
    }

    offset = addr - SDIO_BASE_ADDR;

    switch (offset) {
        case SDIO_POWER_OFF:
            s->power = sdio_write_merge(s->power, addr, value, size) & SDIO_POWER_PWRCTRL_MASK;
            break;

        case SDIO_CLKCR_OFF:
            s->clkcr = sdio_write_merge(s->clkcr, addr, value, size);
            break;

        case SDIO_ARG_OFF:
            s->arg = sdio_write_merge(s->arg, addr, value, size);
            break;

        case SDIO_CMD_OFF: {
            uint32_t merged = sdio_write_merge(s->cmd, addr, value, size);
            sdio_start_command(s, merged);
            break;
        }

        case SDIO_DTIMER_OFF:
            s->dtimer = sdio_write_merge(s->dtimer, addr, value, size);
            break;

        case SDIO_DLEN_OFF:
            s->dlen = sdio_write_merge(s->dlen, addr, value, size);
            s->dcount = s->dlen;
            break;

        case SDIO_DCTRL_OFF:
            s->dctrl = sdio_write_merge(s->dctrl, addr, value, size);
            break;

        case SDIO_ICR_OFF:
            s->sta &= ~sdio_write_merge(0, addr, value, size);
            break;

        case SDIO_MASK_OFF:
            s->mask = sdio_write_merge(s->mask, addr, value, size);
            break;

        default:
            if ((offset >= SDIO_FIFO_OFF) && (offset <= SDIO_FIFO_END)) {
                return;
            }
            return;
    }
}
