// This file is for registering and handling the various signals that peripherals
// can raise to each other and to the CPU, such as external interrupts (EXTI).
#include "boardrunner/signals.h"
#include <stddef.h>

typedef struct {
    signal_handler_t cb;
    void *opaque;
    bool level;
    bool initialized;
} SignalListener;

static SignalListener signal_registry[MAX_SIGNALS];

static bool signal_id_is_valid(int signal_id)
{
    return signal_id >= 0 && signal_id < MAX_SIGNALS;
}

void api_signal_register(int signal_id, signal_handler_t handler, void *opaque)
{
    SignalListener *listener;

    if (!signal_id_is_valid(signal_id)) {
        return;
    }

    listener = &signal_registry[signal_id];
    listener->cb = handler;
    listener->opaque = opaque;
}

void api_signal_set(int signal_id, bool level)
{
    SignalListener *listener;

    if (!signal_id_is_valid(signal_id)) {
        return;
    }

    listener = &signal_registry[signal_id];

    if (listener->initialized && listener->level == level) {
        return;
    }

    listener->level = level;
    listener->initialized = true;

    if (listener->cb != NULL) {
        listener->cb(listener->opaque, signal_id, level);
    }
}
