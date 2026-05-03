#include <stdio.h>
#include <stdint.h>

#include "fuzz.h"

extern void fuzz_libafl_report_assert(bool fatal);
extern uint32_t fuzz_libAFL_init(void);
extern uint64_t fuzz_libafl_wait_next(uint64_t after_epoch, const uint8_t **data, size_t *len);
extern void fuzz_libafl_complete(uint64_t epoch);

static uint64_t g_current_epoch;

bool fuzz_backend_init(void) {
    if (fuzz_libAFL_init() != 1) {
        fprintf(stderr, "Failed to initialize libAFL\n");
        return false;
    }

    return true;
}

bool fuzz_backend_next(fuzz_backend_msg_t *msg)
{
    if (msg == NULL) {
        return false;
    }

    if (g_current_epoch != 0) {
        fuzz_libafl_complete(g_current_epoch);
    }

    const uint8_t *data = NULL;
    size_t len = 0;
    uint64_t next_epoch = fuzz_libafl_wait_next(g_current_epoch, &data, &len);
    if (next_epoch == 0) {
        return false;
    }

    g_current_epoch = next_epoch;
    msg->data = data;
    msg->len = len;
    msg->restore = true;
    return true;
}

void fuzz_backend_report_assert(bool fatal)
{
    fuzz_libafl_report_assert(fatal);
    if (fatal && g_current_epoch != 0) {
        fuzz_libafl_complete(g_current_epoch);
    }
}

void fuzz_backend_set_data(const uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
}

void fuzz_backend_restore_complete(void)
{
    (void)0;
}
