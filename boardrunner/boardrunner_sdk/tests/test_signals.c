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
    CallbackState state2 = {0};
    CallbackState state3 = {0};

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

    api_signal_register(13, test_signal_handler, &state2);
    expect_true(state2.call_count == 1, "late subscriber should receive replay");
    expect_true(state2.last_signal_id == 13, "replay should use the signal id");
    expect_true(state2.last_level == false, "replay should use the current level");
    expect_true(state2.last_opaque == &state2, "replay should use the new opaque");

    api_signal_set(13, true);
    expect_true(state.call_count == 3, "first listener should receive fanout update");
    expect_true(state2.call_count == 2, "second listener should receive fanout update");

    api_signal_unregister(13, test_signal_handler, &state);
    api_signal_set(13, false);
    expect_true(state.call_count == 3, "unregistered listener should stop notifications");
    expect_true(state2.call_count == 3, "remaining listener should still receive updates");

    api_signal_unregister_all(13);
    api_signal_set(13, true);
    expect_true(state.call_count == 3, "unregister_all should keep first listener removed");
    expect_true(state2.call_count == 3, "unregister_all should remove remaining listeners");

    api_signal_set(23, true);
    api_signal_register(23, test_signal_handler, &state3);
    expect_true(state3.call_count == 1, "register after set should replay high level");
    api_signal_register(23, NULL, NULL);
    api_signal_set(23, false);
    expect_true(state3.call_count == 1, "registering NULL should clear listeners for compatibility");

    return 0;
}
