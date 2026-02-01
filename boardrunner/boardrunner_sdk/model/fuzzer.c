#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#ifndef FUZZ_MAX
#define FUZZ_MAX 2048
#endif

// MUST match the anchor args you configured: args = ["1:0xFFFFFFFF"]
#ifndef FUZZ_ANCHOR_ID
#define FUZZ_ANCHOR_ID 1u
#endif

// How long we allow a testcase to “live” after we inject the mutated request
// (poll is ~5ms in your log, so 300 ticks ~ 1.5s)
#ifndef FUZZ_CASE_BUDGET_TICKS
#define FUZZ_CASE_BUDGET_TICKS 300u
#endif

// From fuzz.c
typedef struct {
    _Atomic uint32_t epoch;
    _Atomic uint32_t want_new;
    _Atomic uint32_t ready;
    uint32_t len;
    uint8_t  buf[FUZZ_MAX];
} FuzzShared;

extern FuzzShared g_fuzz;

// Provided by fuzz.c (you already have these)
extern void     fuzz_request_new_input(void);
extern uint32_t fuzz_consume_input_if_ready(uint32_t *io_last_epoch,
                                            uint8_t *out,
                                            uint32_t out_max);

// Provided by your fuzz runtime (already exists in your system)
extern void fuzz_finish(uint32_t anchor_id);
