#include <stdint.h>
#include <string.h>
#include <device.h>

#define RCC_BASE_ADDR          0x40023800ULL

#define RCC_CR_OFF             0x00
#define RCC_PLLCFGR_OFF        0x04
#define RCC_CFGR_OFF           0x08
#define RCC_CIR_OFF            0x0C
#define RCC_AHB1RSTR_OFF       0x10
#define RCC_AHB2RSTR_OFF       0x14
#define RCC_AHB3RSTR_OFF       0x18
#define RCC_APB1RSTR_OFF       0x20
#define RCC_APB2RSTR_OFF       0x24
#define RCC_AHB1ENR_OFF        0x30
#define RCC_AHB2ENR_OFF        0x34
#define RCC_AHB3ENR_OFF        0x38
#define RCC_APB1ENR_OFF        0x40
#define RCC_APB2ENR_OFF        0x44
#define RCC_AHB1LPENR_OFF      0x50
#define RCC_AHB2LPENR_OFF      0x54
#define RCC_AHB3LPENR_OFF      0x58
#define RCC_APB1LPENR_OFF      0x60
#define RCC_APB2LPENR_OFF      0x64
#define RCC_BDCR_OFF           0x70
#define RCC_CSR_OFF            0x74
#define RCC_SSCGR_OFF          0x80
#define RCC_PLLI2SCFGR_OFF     0x84
#define RCC_PLLSAICFGR_OFF     0x88
#define RCC_DCKCFGR_OFF        0x8C
#define RCC_CKGATENR_OFF       0x90
#define RCC_DCKCFGR2_OFF       0x94

#define RCC_CR_HSION           (1U << 0)
#define RCC_CR_HSIRDY          (1U << 1)
#define RCC_CR_HSITRIM_Msk     (0x1FU << 3)
#define RCC_CR_HSEON           (1U << 16)
#define RCC_CR_HSERDY          (1U << 17)
#define RCC_CR_HSEBYP          (1U << 18)
#define RCC_CR_PLLON           (1U << 24)
#define RCC_CR_PLLRDY          (1U << 25)
#define RCC_CR_PLLI2SON        (1U << 26)
#define RCC_CR_PLLI2SRDY       (1U << 27)
#define RCC_CR_PLLSAION        (1U << 28)
#define RCC_CR_PLLSAIRDY       (1U << 29)

#define RCC_CFGR_SW_Msk        (0x3U << 0)
#define RCC_CFGR_SWS_Msk       (0x3U << 2)

#define RCC_CSR_LSION          (1U << 0)
#define RCC_CSR_LSIRDY         (1U << 1)

typedef struct {
    uint32_t cr;
    uint32_t pllcfgr;
    uint32_t cfgr;
    uint32_t cir;
    uint32_t ahb1rstr;
    uint32_t ahb2rstr;
    uint32_t ahb3rstr;
    uint32_t apb1rstr;
    uint32_t apb2rstr;
    uint32_t ahb1enr;
    uint32_t ahb2enr;
    uint32_t ahb3enr;
    uint32_t apb1enr;
    uint32_t apb2enr;
    uint32_t ahb1lpenr;
    uint32_t ahb2lpenr;
    uint32_t ahb3lpenr;
    uint32_t apb1lpenr;
    uint32_t apb2lpenr;
    uint32_t bdcr;
    uint32_t csr;
    uint32_t sscgr;
    uint32_t plli2scfgr;
    uint32_t pllsaicfgr;
    uint32_t dckcfgr;
    uint32_t ckgatenr;
    uint32_t dckcfgr2;
} RCCState;

static RCCState g_rcc;

static uint32_t rcc_cr_readback(RCCState *s) {
    uint32_t v = s->cr;

    v &= ~(RCC_CR_HSIRDY |
           RCC_CR_HSERDY |
           RCC_CR_PLLRDY |
           RCC_CR_PLLI2SRDY |
           RCC_CR_PLLSAIRDY);

    if (v & RCC_CR_HSION) {
        v |= RCC_CR_HSIRDY;
    }
    if (v & RCC_CR_HSEON) {
        v |= RCC_CR_HSERDY;
    }
    if (v & RCC_CR_PLLON) {
        v |= RCC_CR_PLLRDY;
    }
    if (v & RCC_CR_PLLI2SON) {
        v |= RCC_CR_PLLI2SRDY;
    }
    if (v & RCC_CR_PLLSAION) {
        v |= RCC_CR_PLLSAIRDY;
    }

    return v;
}

static uint32_t rcc_cfgr_readback(RCCState *s) {
    uint32_t v = s->cfgr;
    uint32_t sw = v & RCC_CFGR_SW_Msk;
    uint32_t sws = 0;

    switch (sw) {
    case 0x0:
        sws = 0x0;
        break;
    case 0x1:
        sws = (s->cr & RCC_CR_HSEON) ? (0x1U << 2) : 0x0;
        break;
    case 0x2:
        sws = (s->cr & RCC_CR_PLLON) ? (0x2U << 2) : 0x0;
        break;
    default:
        sws = sw << 2;
        break;
    }

    v &= ~RCC_CFGR_SWS_Msk;
    v |= sws;
    return v;
}

static uint32_t rcc_csr_readback(RCCState *s) {
    uint32_t v = s->csr;

    v &= ~RCC_CSR_LSIRDY;
    if (v & RCC_CSR_LSION) {
        v |= RCC_CSR_LSIRDY;
    }

    return v;
}

static uint32_t rcc_read_reg32(RCCState *s, uint32_t reg_off) {
    switch (reg_off) {
    case RCC_CR_OFF:
        return rcc_cr_readback(s);
    case RCC_PLLCFGR_OFF:
        return s->pllcfgr;
    case RCC_CFGR_OFF:
        return rcc_cfgr_readback(s);
    case RCC_CIR_OFF:
        return s->cir;
    case RCC_AHB1RSTR_OFF:
        return s->ahb1rstr;
    case RCC_AHB2RSTR_OFF:
        return s->ahb2rstr;
    case RCC_AHB3RSTR_OFF:
        return s->ahb3rstr;
    case RCC_APB1RSTR_OFF:
        return s->apb1rstr;
    case RCC_APB2RSTR_OFF:
        return s->apb2rstr;
    case RCC_AHB1ENR_OFF:
        return s->ahb1enr;
    case RCC_AHB2ENR_OFF:
        return s->ahb2enr;
    case RCC_AHB3ENR_OFF:
        return s->ahb3enr;
    case RCC_APB1ENR_OFF:
        return s->apb1enr;
    case RCC_APB2ENR_OFF:
        return s->apb2enr;
    case RCC_AHB1LPENR_OFF:
        return s->ahb1lpenr;
    case RCC_AHB2LPENR_OFF:
        return s->ahb2lpenr;
    case RCC_AHB3LPENR_OFF:
        return s->ahb3lpenr;
    case RCC_APB1LPENR_OFF:
        return s->apb1lpenr;
    case RCC_APB2LPENR_OFF:
        return s->apb2lpenr;
    case RCC_BDCR_OFF:
        return s->bdcr;
    case RCC_CSR_OFF:
        return rcc_csr_readback(s);
    case RCC_SSCGR_OFF:
        return s->sscgr;
    case RCC_PLLI2SCFGR_OFF:
        return s->plli2scfgr;
    case RCC_PLLSAICFGR_OFF:
        return s->pllsaicfgr;
    case RCC_DCKCFGR_OFF:
        return s->dckcfgr;
    case RCC_CKGATENR_OFF:
        return s->ckgatenr;
    case RCC_DCKCFGR2_OFF:
        return s->dckcfgr2;
    default:
        return 0;
    }
}

static void rcc_write_reg32(RCCState *s, uint32_t reg_off, uint32_t value) {
    switch (reg_off) {
    case RCC_CR_OFF:
        s->cr = value & ~(RCC_CR_HSIRDY |
                          RCC_CR_HSERDY |
                          RCC_CR_PLLRDY |
                          RCC_CR_PLLI2SRDY |
                          RCC_CR_PLLSAIRDY);
        break;
    case RCC_PLLCFGR_OFF:
        s->pllcfgr = value;
        break;
    case RCC_CFGR_OFF:
        s->cfgr = value;
        break;
    case RCC_CIR_OFF:
        s->cir = value;
        break;
    case RCC_AHB1RSTR_OFF:
        s->ahb1rstr = value;
        break;
    case RCC_AHB2RSTR_OFF:
        s->ahb2rstr = value;
        break;
    case RCC_AHB3RSTR_OFF:
        s->ahb3rstr = value;
        break;
    case RCC_APB1RSTR_OFF:
        s->apb1rstr = value;
        break;
    case RCC_APB2RSTR_OFF:
        s->apb2rstr = value;
        break;
    case RCC_AHB1ENR_OFF:
        s->ahb1enr = value;
        break;
    case RCC_AHB2ENR_OFF:
        s->ahb2enr = value;
        break;
    case RCC_AHB3ENR_OFF:
        s->ahb3enr = value;
        break;
    case RCC_APB1ENR_OFF:
        s->apb1enr = value;
        break;
    case RCC_APB2ENR_OFF:
        s->apb2enr = value;
        break;
    case RCC_AHB1LPENR_OFF:
        s->ahb1lpenr = value;
        break;
    case RCC_AHB2LPENR_OFF:
        s->ahb2lpenr = value;
        break;
    case RCC_AHB3LPENR_OFF:
        s->ahb3lpenr = value;
        break;
    case RCC_APB1LPENR_OFF:
        s->apb1lpenr = value;
        break;
    case RCC_APB2LPENR_OFF:
        s->apb2lpenr = value;
        break;
    case RCC_BDCR_OFF:
        s->bdcr = value;
        break;
    case RCC_CSR_OFF:
        s->csr = value & ~RCC_CSR_LSIRDY;
        break;
    case RCC_SSCGR_OFF:
        s->sscgr = value;
        break;
    case RCC_PLLI2SCFGR_OFF:
        s->plli2scfgr = value;
        break;
    case RCC_PLLSAICFGR_OFF:
        s->pllsaicfgr = value;
        break;
    case RCC_DCKCFGR_OFF:
        s->dckcfgr = value;
        break;
    case RCC_CKGATENR_OFF:
        s->ckgatenr = value;
        break;
    case RCC_DCKCFGR2_OFF:
        s->dckcfgr2 = value;
        break;
    default:
        break;
    }
}

void* rcc_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_rcc, 0, sizeof(g_rcc));
    return &g_rcc;
}

uint64_t rcc_read(void *opaque, uint64_t addr, unsigned size) {
    RCCState *s = (RCCState *)opaque;
    uint64_t offset;
    uint32_t reg_off;
    uint32_t reg_val;
    unsigned shift;

    if (s == NULL) {
        s = &g_rcc;
    }

    if (addr < RCC_BASE_ADDR) {
        return 0;
    }

    offset = addr - RCC_BASE_ADDR;
    reg_off = (uint32_t)(offset & ~0x3ULL);
    shift = (unsigned)((offset & 0x3ULL) * 8U);
    reg_val = rcc_read_reg32(s, reg_off);

    switch (size) {
    case 1:
        return (reg_val >> shift) & 0xFFU;
    case 2:
        return (reg_val >> shift) & 0xFFFFU;
    case 4:
    default:
        return reg_val;
    }
}

void rcc_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    RCCState *s = (RCCState *)opaque;
    uint64_t offset;
    uint32_t reg_off;
    uint32_t cur;
    uint32_t mask;
    unsigned shift;

    if (s == NULL) {
        s = &g_rcc;
    }

    if (addr < RCC_BASE_ADDR) {
        return;
    }

    offset = addr - RCC_BASE_ADDR;
    reg_off = (uint32_t)(offset & ~0x3ULL);
    shift = (unsigned)((offset & 0x3ULL) * 8U);

    if (size == 4 && shift == 0) {
        rcc_write_reg32(s, reg_off, (uint32_t)value);
        return;
    }

    cur = rcc_read_reg32(s, reg_off);

    switch (size) {
    case 1:
        mask = 0xFFU;
        break;
    case 2:
        mask = 0xFFFFU;
        break;
    case 4:
    default:
        mask = 0xFFFFFFFFU;
        break;
    }

    cur &= ~(mask << shift);
    cur |= (((uint32_t)value) & mask) << shift;
    rcc_write_reg32(s, reg_off, cur);
}
