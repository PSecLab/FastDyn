#ifndef PROBE_H
#define PROBE_H

#include <stdbool.h>
#include <stdint.h>

#include <config.h>

#if ENABLE_PROBE

int probe_init(int argc, char **argv);
bool probe_is_enabled(void);

void probe_check_read(uint64_t pc, uint64_t addr, uint64_t value);
void probe_check_write(uint64_t pc, uint64_t addr, uint64_t value);
void probe_check_unhandled_access(uint64_t pc, uint64_t addr, int is_write);

#else

static inline int probe_init(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

static inline bool probe_is_enabled(void)
{
    return false;
}

static inline void probe_check_read(uint64_t pc, uint64_t addr, uint64_t value)
{
    (void)pc;
    (void)addr;
    (void)value;
}

static inline void probe_check_write(uint64_t pc, uint64_t addr, uint64_t value)
{
    (void)pc;
    (void)addr;
    (void)value;
}

static inline void probe_check_unhandled_access(uint64_t pc, uint64_t addr, int is_write)
{
    (void)pc;
    (void)addr;
    (void)is_write;
}

#endif

#endif /* PROBE_H */
