#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <device.h>

#define STM32F427_FLASH_BASE            0x40023C00ULL

#define FLASH_ACR_OFFSET                0x00U

#define FLASH_ACR_LATENCY_Msk           0x0000000FU
#define FLASH_ACR_PRFTEN                0x00000100U
#define FLASH_ACR_ICEN                  0x00000200U
#define FLASH_ACR_DCEN                  0x00000400U
#define FLASH_ACR_ICRST                 0x00000800U
#define FLASH_ACR_DCRST                 0x00001000U
#define FLASH_ACR_WRITABLE_MASK         (FLASH_ACR_LATENCY_Msk | \
                                         FLASH_ACR_PRFTEN | \
                                         FLASH_ACR_ICEN | \
                                         FLASH_ACR_DCEN | \
                                         FLASH_ACR_ICRST | \
                                         FLASH_ACR_DCRST)

typedef struct {
    uint32_t acr;
} FlashState;

static FlashState g_flash;

static void flash_log_bad_access(const char *op, uint64_t addr, unsigned size, uint64_t value) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "FLASH %s unknown addr=0x%08llx size=%u value=0x%08llx",
             op,
             (unsigned long long)addr,
             size,
             (unsigned long long)value);
    dev_debug(buf);
}

static uint64_t flash_read_part32(uint32_t reg, uint64_t offset, unsigned size) {
    unsigned byte_shift = (unsigned)(offset & 0x3ULL) * 8U;

    if (size >= 4) {
        return (uint64_t)reg;
    }

    return ((uint64_t)reg >> byte_shift) & ((1ULL << (size * 8U)) - 1ULL);
}

static uint32_t flash_write_part32(uint32_t old_reg, uint64_t offset, uint64_t value, unsigned size) {
    unsigned byte_shift = (unsigned)(offset & 0x3ULL) * 8U;
    uint32_t mask;

    if (size >= 4) {
        mask = 0xFFFFFFFFU;
    } else {
        mask = (uint32_t)(((1ULL << (size * 8U)) - 1ULL) << byte_shift);
    }

    return (old_reg & ~mask) | (((uint32_t)value << byte_shift) & mask);
}

void* flash_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_flash, 0, sizeof(g_flash));
    g_flash.acr = 0x00000000U;

    return &g_flash;
}

uint64_t flash_read(void *opaque, uint64_t addr, unsigned size) {
    FlashState *s = (FlashState *)opaque;
    uint64_t offset;

    if (s == NULL) {
        s = &g_flash;
    }

    if (addr < STM32F427_FLASH_BASE) {
        flash_log_bad_access("read", addr, size, 0);
        return 0;
    }

    offset = addr - STM32F427_FLASH_BASE;

    if ((offset <= 0x3U) && (offset + size <= 0x4U)) {
        return flash_read_part32(s->acr, offset, size);
    }

    flash_log_bad_access("read", addr, size, 0);
    return 0;
}

void flash_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    FlashState *s = (FlashState *)opaque;
    uint64_t offset;
    uint32_t new_acr;

    if (s == NULL) {
        s = &g_flash;
    }

    if (addr < STM32F427_FLASH_BASE) {
        flash_log_bad_access("write", addr, size, value);
        return;
    }

    offset = addr - STM32F427_FLASH_BASE;

    if ((offset <= 0x3U) && (offset + size <= 0x4U)) {
        new_acr = flash_write_part32(s->acr, offset, value, size);
        s->acr = new_acr & FLASH_ACR_WRITABLE_MASK;
        return;
    }

    flash_log_bad_access("write", addr, size, value);
}