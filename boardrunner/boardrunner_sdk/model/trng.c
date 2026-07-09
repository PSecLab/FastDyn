#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define TRNG_BASE_ADDR              0x400CC000ULL
#define TRNG_LAST_OFFSET            0xF8U
#define TRNG_REG_WORDS              ((TRNG_LAST_OFFSET / 4U) + 1U)

#define TRNG_MCTL_OFFSET            0x00U

#define TRNG_MCTL_RST_DEF_MASK      0x00000040U
#define TRNG_MCTL_ENT_VAL_MASK      0x00000400U
#define TRNG_MCTL_ERR_MASK          0x00001000U
#define TRNG_MCTL_TSTOP_OK_MASK     0x00002000U
#define TRNG_MCTL_PRGM_MASK         0x00010000U

#define TRNG_ENT_COUNT              16U
#define TRNG_ENT_WINDOW_BASE        0x40U
#define TRNG_ENT_WINDOW_SIZE        (TRNG_ENT_COUNT * 4U)

typedef struct {
    uint32_t regs[TRNG_REG_WORDS];
    uint32_t entropy[TRNG_ENT_COUNT];
    uint16_t entropy_seen_mask;
    uint32_t mctl_ctrl;
    uint64_t prng_state;
    bool entropy_valid;
    bool error_flag;
} TrngState;

static TrngState g_trng_state;

static uint32_t trng_mask_for_size(unsigned size)
{
    switch (size) {
    case 1:
        return 0xFFU;
    case 2:
        return 0xFFFFU;
    default:
        return 0xFFFFFFFFU;
    }
}

static bool trng_program_mode(const TrngState *s)
{
    return (s->mctl_ctrl & TRNG_MCTL_PRGM_MASK) != 0U;
}

static uint32_t trng_next_u32(TrngState *s)
{
    uint64_t z;

    if (s->prng_state == 0ULL) {
        uint64_t seed = (uint64_t)qemu_plugin_get_virtual_timer();
        if (seed == 0ULL) {
            seed = 0xD1B54A32D192ED03ULL;
        }
        s->prng_state = seed ^ 0x9E3779B97F4A7C15ULL ^ TRNG_BASE_ADDR;
    }

    s->prng_state += 0x9E3779B97F4A7C15ULL;
    z = s->prng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= (z >> 31);

    return (uint32_t)(z ^ (z >> 32));
}

static void trng_fill_entropy(TrngState *s)
{
    unsigned i;

    for (i = 0; i < TRNG_ENT_COUNT; i++) {
        s->entropy[i] = trng_next_u32(s);
    }

    s->entropy_seen_mask = 0U;
    s->entropy_valid = true;
}

static void trng_reset_programmable_state(TrngState *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->entropy, 0, sizeof(s->entropy));
    s->entropy_seen_mask = 0U;
    s->entropy_valid = false;
    s->error_flag = false;
}

static bool trng_entropy_index(uint32_t offset, unsigned *index_out)
{
    if ((offset >= TRNG_ENT_WINDOW_BASE) &&
        (offset < (TRNG_ENT_WINDOW_BASE + TRNG_ENT_WINDOW_SIZE)) &&
        (((offset - TRNG_ENT_WINDOW_BASE) & 0x3U) == 0U)) {
        *index_out = (offset - TRNG_ENT_WINDOW_BASE) / 4U;
        return true;
    }

    return false;
}

static uint32_t trng_read_mctl(TrngState *s)
{
    uint32_t value = s->mctl_ctrl;

    if (trng_program_mode(s)) {
        value |= TRNG_MCTL_TSTOP_OK_MASK;
    } else {
        if (!s->entropy_valid && !s->error_flag) {
            trng_fill_entropy(s);
        }
        if (s->entropy_valid) {
            value |= TRNG_MCTL_ENT_VAL_MASK;
        }
    }

    if (s->error_flag) {
        value |= TRNG_MCTL_ERR_MASK;
    }

    return value;
}

static void trng_write_mctl(TrngState *s, uint32_t value)
{
    bool program_mode = (value & TRNG_MCTL_PRGM_MASK) != 0U;

    if ((value & TRNG_MCTL_RST_DEF_MASK) != 0U) {
        trng_reset_programmable_state(s);
    }

    if ((value & TRNG_MCTL_ERR_MASK) != 0U) {
        s->error_flag = false;
    }

    s->mctl_ctrl = value &
                   ~(TRNG_MCTL_RST_DEF_MASK |
                     TRNG_MCTL_ENT_VAL_MASK |
                     TRNG_MCTL_ERR_MASK |
                     TRNG_MCTL_TSTOP_OK_MASK);

    if (program_mode) {
        s->entropy_valid = false;
        s->entropy_seen_mask = 0U;
    } else {
        trng_fill_entropy(s);
    }
}

static uint32_t trng_read_entropy_word(TrngState *s, unsigned index)
{
    uint32_t value;

    if (index >= TRNG_ENT_COUNT) {
        return 0U;
    }

    if (trng_program_mode(s)) {
        return 0U;
    }

    if (!s->entropy_valid) {
        trng_fill_entropy(s);
    }

    value = s->entropy[index];
    s->entropy_seen_mask |= (uint16_t)(1U << index);

    if (s->entropy_seen_mask == 0xFFFFU) {
        trng_fill_entropy(s);
    }

    return value;
}

static uint32_t trng_read_word(TrngState *s, uint32_t offset)
{
    unsigned ent_index;

    if (offset == TRNG_MCTL_OFFSET) {
        return trng_read_mctl(s);
    }

    if (trng_entropy_index(offset, &ent_index)) {
        return trng_read_entropy_word(s, ent_index);
    }

    if ((offset & 0x3U) != 0U) {
        return 0U;
    }

    if ((offset / 4U) >= TRNG_REG_WORDS) {
        return 0U;
    }

    return s->regs[offset / 4U];
}

static void trng_write_word(TrngState *s, uint32_t offset, uint32_t value)
{
    unsigned ent_index;

    if (offset == TRNG_MCTL_OFFSET) {
        trng_write_mctl(s, value);
        return;
    }

    if (trng_entropy_index(offset, &ent_index)) {
        (void)ent_index;
        return;
    }

    if ((offset & 0x3U) != 0U) {
        return;
    }

    if ((offset / 4U) >= TRNG_REG_WORDS) {
        return;
    }

    s->regs[offset / 4U] = value;
}

void* trng_init(ConfigSection* model_info)
{
    uint64_t seed;

    (void)model_info;

    memset(&g_trng_state, 0, sizeof(g_trng_state));

    seed = (uint64_t)qemu_plugin_get_virtual_timer();
    if (seed == 0ULL) {
        seed = 0xA0761D6478BD642FULL;
    }
    g_trng_state.prng_state = seed ^ 0xE7037ED1A0B428DBULL ^ TRNG_BASE_ADDR;

    return &g_trng_state;
}

uint64_t trng_read(void *opaque, uint64_t addr, unsigned size)
{
    TrngState *s = (TrngState *)opaque;
    uint64_t offset;
    uint32_t word;
    uint32_t shift;
    uint32_t mask;

    if (s == NULL) {
        s = &g_trng_state;
    }

    if (addr < TRNG_BASE_ADDR) {
        return 0;
    }

    offset = addr - TRNG_BASE_ADDR;
    if (offset > TRNG_LAST_OFFSET) {
        return 0;
    }

    word = trng_read_word(s, (uint32_t)(offset & ~0x3ULL));

    if (size >= 4U) {
        return word;
    }

    shift = (uint32_t)((offset & 0x3ULL) * 8ULL);
    mask = trng_mask_for_size(size);

    return (word >> shift) & mask;
}

void trng_write(void *opaque, uint64_t addr, uint64_t value, unsigned size)
{
    TrngState *s = (TrngState *)opaque;
    uint64_t offset;
    uint32_t aligned_offset;
    uint32_t new_word;

    if (s == NULL) {
        s = &g_trng_state;
    }

    if (addr < TRNG_BASE_ADDR) {
        return;
    }

    offset = addr - TRNG_BASE_ADDR;
    if (offset > TRNG_LAST_OFFSET) {
        return;
    }

    aligned_offset = (uint32_t)(offset & ~0x3ULL);

    if ((size >= 4U) && ((offset & 0x3ULL) == 0ULL)) {
        trng_write_word(s, aligned_offset, (uint32_t)value);
        return;
    }

    {
        uint32_t current = trng_read_word(s, aligned_offset);
        uint32_t shift = (uint32_t)((offset & 0x3ULL) * 8ULL);
        uint32_t field_mask = trng_mask_for_size(size) << shift;

        new_word = (current & ~field_mask) |
                   (((uint32_t)value << shift) & field_mask);
    }

    trng_write_word(s, aligned_offset, new_word);
}
