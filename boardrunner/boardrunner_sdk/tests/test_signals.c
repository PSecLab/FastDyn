#include <boardrunner/signals.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int call_count;
    int last_signal_id;
    bool last_level;
    void *last_opaque;
} CallbackState;

static void test_signal_handler(void *opaque, int signal_id, bool level)
{
    CallbackState *state = (CallbackState *)opaque;

    state->call_count++;
    state->last_signal_id = signal_id;
    state->last_level = level;
    state->last_opaque = opaque;
}

static void expect_true(bool cond, const char *message)
{
    if (!cond) {
        fprintf(stderr, "test_signals: %s\n", message);
        exit(1);
    }
}

int main(void)
{
    CallbackState state = {0};
    int untouched = 1234;

    api_signal_set(-1, true);
    api_signal_set(MAX_SIGNALS, false);
    api_signal_register(-1, test_signal_handler, &state);
    api_signal_register(MAX_SIGNALS, test_signal_handler, &state);

    api_signal_set(7, true);
    expect_true(state.call_count == 0, "unregistered signal should not notify");

    api_signal_register(13, test_signal_handler, &state);

    api_signal_set(13, true);
    expect_true(state.call_count == 1, "registered signal should notify once");
    expect_true(state.last_signal_id == 13, "callback should receive the signal id");
    expect_true(state.last_level == true, "callback should receive the logical level");
    expect_true(state.last_opaque == &state, "callback should receive the opaque pointer");

    api_signal_set(13, true);
    expect_true(state.call_count == 1, "same level should not notify twice");

    api_signal_set(13, false);
    expect_true(state.call_count == 2, "level transition should notify");
    expect_true(state.last_level == false, "callback should observe falling level");

    api_signal_register(13, NULL, &untouched);
    api_signal_set(13, true);
    api_signal_set(13, false);
    expect_true(state.call_count == 2, "clearing the handler should stop notifications");

    return 0;
}
