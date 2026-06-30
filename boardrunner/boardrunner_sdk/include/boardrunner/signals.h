#ifndef BOARDRUNNER_SIGNALS_H
#define BOARDRUNNER_SIGNALS_H

#include <stdbool.h>

#define MAX_SIGNALS 128
#define MAX_SIGNAL_LISTENERS_PER_SIGNAL 8

/*
 * Generic logical signal IDs used to connect independent peripheral models.
 * For STM32 GPIO/EXTI compositions, use the EXTI line number as the signal ID.
 */

/**
 * @brief Prototype for signal notification callbacks.
 * 
 * @param opaque User-defined pointer passed during registration.
 * @param signal_id The integer ID of the signal transitioning.
 * @param level The new logical level of the signal (true = high, false = low).
 */
typedef void (*signal_handler_t)(void *opaque, int signal_id, bool level);

/**
 * @brief Register a handler to listen for state changes on a specific signal line.
 *
 * The signals API is level-based by design. A source peripheral such as GPIO
 * publishes the logical line level through api_signal_set(), while the sink
 * peripheral (for example EXTI) owns any edge detection and trigger policy.
 * Multiple handlers may subscribe to the same signal. If the signal already
 * has a published level, the new subscriber is called immediately with that
 * current level so late subscribers do not miss initial state.
 *
 * @param signal_id The designated logical signal line (e.g., EXTI line number).
 * @param handler The function callback invoked upon signal change.
 * @param opaque Context pointer stored and returned inside the callback.
 */
void api_signal_register(int signal_id, signal_handler_t handler, void *opaque);

/**
 * @brief Unregister a previously registered handler for a specific signal line.
 *
 * The handler and opaque pointer must both match the registration entry.
 *
 * @param signal_id The designated logical signal line.
 * @param handler The callback function to remove.
 * @param opaque Context pointer originally passed during registration.
 */
void api_signal_unregister(int signal_id, signal_handler_t handler, void *opaque);

/**
 * @brief Remove all handlers registered for a specific signal line.
 *
 * This clears subscribers only; the last published signal level is preserved.
 *
 * @param signal_id The designated logical signal line.
 */
void api_signal_unregister_all(int signal_id);

/**
 * @brief Update the logical level of a specific signal line, triggering interested listeners.
 *
 * Repeated updates with the same level are ignored so models do not receive
 * duplicate notifications when firmware rewrites an unchanged state.
 *
 * @param signal_id The designated logical signal line.
 * @param level The new status (true for high, false for low).
 */
void api_signal_set(int signal_id, bool level);

#endif // BOARDRUNNER_SIGNALS_H
