#include <stdint.h>
#include <string.h>
#include <device.h>

#define PWR_BASE            0x40007000ULL

#define PWR_CR_OFFSET       0x00
#define PWR_CSR_OFFSET      0x04

#define PWR_CR_CWUF         (1U << 2)
#define PWR_CR_CSBF         (1U << 3)
#define PWR_CR_VOS_MASK     (3U << 14)
#define PWR_CR_ODEN         (1U << 16)
#define PWR_CR_ODSWEN       (1U << 17)

#define PWR_CSR_WUF         (1U << 0)
#define PWR_CSR_SBF         (1U << 1)
#define PWR_CSR_VOSRDY      (1U << 14)
#define PWR_CSR_ODRDY       (1U << 16)
#define PWR_CSR_ODSWRDY     (1U << 17)

typedef struct {
    uint32_t cr;
    uint32_t csr;
} PWRState;

static PWRState g_pwr;

static void pwr_update_status(PWRState *s) {
    /* The regulator is considered immediately stable for firmware polling. */
    s->csr |= PWR_CSR_VOSRDY;

    if (s->cr & PWR_CR_ODEN) {
        s->csr |= PWR_CSR_ODRDY;
    } else {
        s->csr &= ~PWR_CSR_ODRDY;
    }

    if ((s->cr & PWR_CR_ODEN) && (s->cr & PWR_CR_ODSWEN)) {
        s->csr |= PWR_CSR_ODSWRDY;
    } else {
        s->csr &= ~PWR_CSR_ODSWRDY;
    }
}

void* pwr_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_pwr, 0, sizeof(g_pwr));

    /*
     * After reset the power regulator is already operational; expose VOSRDY so
     * stm32_clock_init() can complete its synchronization wait.
     */
    g_pwr.csr = PWR_CSR_VOSRDY;

    return &g_pwr;
}

uint64_t pwr_read(void *opaque, uint64_t addr, unsigned size) {
    PWRState *s = (PWRState *)opaque;
    uint64_t offset = addr - PWR_BASE;
    uint32_t value = 0;

    (void)size;

    if (s == NULL) {
        s = &g_pwr;
    }

    pwr_update_status(s);

    switch (offset) {
    case PWR_CR_OFFSET:
        value = s->cr;
        break;
    case PWR_CSR_OFFSET:
        value = s->csr;
        break;
    default:
        value = 0;
        break;
    }

    return value;
}

void pwr_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    PWRState *s = (PWRState *)opaque;
    uint64_t offset = addr - PWR_BASE;
    uint32_t v = (uint32_t)value;

    (void)size;

    if (s == NULL) {
        s = &g_pwr;
    }

    switch (offset) {
    case PWR_CR_OFFSET:
        /*
         * Store software-visible control bits. CWUF/CSBF are action bits and
         * self-clear; model only their effect on the corresponding status bits.
         */
        if (v & PWR_CR_CWUF) {
            s->csr &= ~PWR_CSR_WUF;
        }
        if (v & PWR_CR_CSBF) {
            s->csr &= ~PWR_CSR_SBF;
        }

        s->cr = v & ~(PWR_CR_CWUF | PWR_CR_CSBF);
        pwr_update_status(s);
        break;

    case PWR_CSR_OFFSET:
        /*
         * CSR is treated as status-only for the behavior observed in trace.
         */
        break;

    default:
        break;
    }
}
