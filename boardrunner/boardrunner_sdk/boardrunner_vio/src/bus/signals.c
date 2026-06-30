// This file is for registering and handling the various signals that peripherals
// can raise to each other and to the CPU, such as external interrupts (EXTI).
#include "boardrunner/signals.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    signal_handler_t cb;
    void *opaque;
    bool active;
} SignalSubscription;

typedef struct {
    SignalSubscription listeners[MAX_SIGNAL_LISTENERS_PER_SIGNAL];
    bool level;
    bool initialized;
} SignalLine;

static SignalLine signal_registry[MAX_SIGNALS];

static bool signal_id_is_valid(int signal_id)
{
    return signal_id >= 0 && signal_id < MAX_SIGNALS;
}

static void signal_listener_clear(SignalSubscription *listener)
{
    if (listener == NULL) {
        return;
    }

    memset(listener, 0, sizeof(*listener));
}

static SignalSubscription *signal_find_listener(SignalLine *line,
                                                signal_handler_t handler,
                                                void *opaque)
{
    int i;

    if (line == NULL || handler == NULL) {
        return NULL;
    }

    for (i = 0; i < MAX_SIGNAL_LISTENERS_PER_SIGNAL; i++) {
        SignalSubscription *listener = &line->listeners[i];

        if (listener->active &&
            listener->cb == handler &&
            listener->opaque == opaque) {
            return listener;
        }
    }

    return NULL;
}

static SignalSubscription *signal_find_free_listener(SignalLine *line)
{
    int i;

    if (line == NULL) {
        return NULL;
    }

    for (i = 0; i < MAX_SIGNAL_LISTENERS_PER_SIGNAL; i++) {
        SignalSubscription *listener = &line->listeners[i];

        if (!listener->active) {
            return listener;
        }
    }

    return NULL;
}

void api_signal_register(int signal_id, signal_handler_t handler, void *opaque)
{
    SignalLine *line;
    SignalSubscription *listener;

    if (!signal_id_is_valid(signal_id)) {
        return;
    }
    if (handler == NULL) {
        api_signal_unregister_all(signal_id);
        return;
    }

    line = &signal_registry[signal_id];
    listener = signal_find_listener(line, handler, opaque);
    if (listener == NULL) {
        listener = signal_find_free_listener(line);
    }
    if (listener == NULL) {
        return;
    }

    listener->active = true;
    listener->cb = handler;
    listener->opaque = opaque;

    if (line->initialized) {
        handler(opaque, signal_id, line->level);
    }
}

void api_signal_unregister(int signal_id, signal_handler_t handler, void *opaque)
{
    SignalLine *line;
    SignalSubscription *listener;

    if (!signal_id_is_valid(signal_id) || handler == NULL) {
        return;
    }

    line = &signal_registry[signal_id];
    listener = signal_find_listener(line, handler, opaque);
    signal_listener_clear(listener);
}

void api_signal_unregister_all(int signal_id)
{
    SignalLine *line;
    int i;

    if (!signal_id_is_valid(signal_id)) {
        return;
    }

    line = &signal_registry[signal_id];
    for (i = 0; i < MAX_SIGNAL_LISTENERS_PER_SIGNAL; i++) {
        signal_listener_clear(&line->listeners[i]);
    }
}

void api_signal_set(int signal_id, bool level)
{
    SignalLine *line;
    int i;

    if (!signal_id_is_valid(signal_id)) {
        return;
    }

    line = &signal_registry[signal_id];

    if (line->initialized && line->level == level) {
        return;
    }

    line->level = level;
    line->initialized = true;

    for (i = 0; i < MAX_SIGNAL_LISTENERS_PER_SIGNAL; i++) {
        SignalSubscription *listener = &line->listeners[i];
        signal_handler_t cb;
        void *opaque;

        if (!listener->active || listener->cb == NULL) {
            continue;
        }

        cb = listener->cb;
        opaque = listener->opaque;
        cb(opaque, signal_id, level);
    }
}
