#include <device.h> // Provides access to the required plugin APIs
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// --- Device Model for TIM1 ---

// Base address for the TIM1 peripheral
#define TIM1_BASE_ADDR 0x40010000

// Inferred Register Offsets from base address
typedef enum {
    TIM1_CR1_OFFSET  = 0x00,
    TIM1_DIER_OFFSET = 0x0C,
    TIM1_SR_OFFSET   = 0x10,
    TIM1_PSC_OFFSET  = 0x28,
    TIM1_ARR_OFFSET  = 0x2C,
} TIM1_RegOffsets;

// Inferred Register Bitfields
#define CR1_CEN_BIT  (1 << 0) // Counter Enable bit in CR1
#define DIER_UIE_BIT (1 << 0) // Update Interrupt Enable bit in DIER
#define SR_UIF_BIT   (1 << 0) // Update Interrupt Flag bit in SR

// The other status flags that are consistently read as set in the trace.
#define SR_OTHER_FLAGS 0x1F

// A plausible clock frequency based on PSC/ARR values and the ~1s interrupt period.
#define TIM1_CLK_FREQ_HZ 16000000ULL

// Structure to hold the internal state of the emulated TIM1 device
typedef struct {
    uint32_t cr1;
    uint32_t dier;
    uint32_t sr;
    uint32_t psc;
    uint32_t arr;
    uint64_t timer_fd; // Handle for the QEMU timer used to generate interrupts
} tim1_state_t;

// A single static instance of our device's state
static tim1_state_t tim1_device_state;

// Forward declaration for the timer callback function
static void tim1_timer_callback(void *opaque);

/**
 * @brief Initializes the TIM1 device model state.
 * This function is called once by the emulation harness to set up the device.
 */
void tim1_init(void *opaque) {
    // Reset the device state to a clean slate
    memset(&tim1_device_state, 0, sizeof(tim1_state_t));

    // Create a new one-shot timer to generate our interrupts. We will re-arm it as needed.
    // The user data pointer is set to our device state struct.
    tim1_device_state.timer_fd = qemu_plugin_timer_new_ns(tim1_timer_callback, &tim1_device_state);
    dev_debug("TIM1: Device model initialized and timer created.");
}

/**
 * @brief This callback is executed by QEMU when our timer fires.
 * It models the timer's update event.
 */
static void tim1_timer_callback(void *opaque) {
    tim1_state_t *s = (tim1_state_t *)opaque;

    // Check if the Update Interrupt is actually enabled in the DIER register
    if (s->dier & DIER_UIE_BIT) {
        dev_debug("TIM1: Timer expired. Setting Update Interrupt Flag (UIF).");
        // Set the Update Interrupt Flag (UIF) in our status register
        s->sr |= SR_UIF_BIT;

        dev_debug("TIM1: Raising IRQ 25.");
        // Raise the interrupt line for TIM1 (Vector 25 as seen in trace)
        qemu_plugin_raise_irq(25+16);
    }
}

/**
 * @brief Emulates all MMIO reads from the TIM1 device registers.
 */
uint64_t tim1_read(void *opaque, hwaddr addr, unsigned size) {
    tim1_state_t *s = &tim1_device_state;
    uint32_t offset = addr - TIM1_BASE_ADDR;
    uint32_t retval = 0;
    char msg[128];

    switch (offset) {
        case TIM1_CR1_OFFSET:
            retval = s->cr1;
            break;

        case TIM1_DIER_OFFSET:
            retval = s->dier;
            break;

        case TIM1_SR_OFFSET:
            // The trace shows reads of 0x1F, meaning other flags are also set.
            // We combine our dynamically managed UIF flag with these static flags.
            retval = s->sr | SR_OTHER_FLAGS;
            break;

        default:
            snprintf(msg, sizeof(msg), "TIM1: Read from unhandled offset 0x%x", offset);
            dev_debug(msg);
            break;
    }
    snprintf(msg, sizeof(msg), "TIM1: READ from offset 0x%x, value 0x%x", offset, retval);
    dev_debug(msg);
    return retval;
}

/**
 * @brief Emulates all MMIO writes to the TIM1 device registers.
 */
void tim1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    tim1_state_t *s = &tim1_device_state;
    uint32_t offset = addr - TIM1_BASE_ADDR;
    char msg[128];

    snprintf(msg, sizeof(msg), "TIM1: WRITE to offset 0x%x, value 0x%llx", offset, (unsigned long long)value);
    dev_debug(msg);

    switch (offset) {
        case TIM1_PSC_OFFSET:
            s->psc = value;
            break;

        case TIM1_ARR_OFFSET:
            s->arr = value;
            break;

        case TIM1_DIER_OFFSET:
            s->dier = value;
            break;

        case TIM1_CR1_OFFSET: {
            uint32_t old_cen = s->cr1 & CR1_CEN_BIT;
            s->cr1 = value;
            uint32_t new_cen = s->cr1 & CR1_CEN_BIT;

            // Detect the rising edge of the Counter Enable (CEN) bit.
            if (!old_cen && new_cen) {
                dev_debug("TIM1: Counter enabled. Firing immediate update event.");
                // The trace shows an interrupt occurs almost immediately after the timer
                // is enabled. This models the initial update event to load PSC/ARR.
                // We fire the callback directly to set the flag and raise the IRQ.
                tim1_timer_callback(s);
            }
            break;
        }

        case TIM1_SR_OFFSET: {
             uint32_t uif_was_set = s->sr & SR_UIF_BIT;
             // On STM32, writing 0 to a status flag clears it.
             s->sr &= value;
             uint32_t uif_is_set = s->sr & SR_UIF_BIT;

             // Detect if the UIF bit was just cleared by the software write.
             if (uif_was_set && !uif_is_set) {
                dev_debug("TIM1: UIF cleared by software.");
                // If the timer is still enabled, we must arm the timer for the next periodic interrupt.
                if (s->cr1 & CR1_CEN_BIT) {
                    uint64_t period_ticks = (uint64_t)(s->psc + 1) * (s->arr + 1);
                    uint64_t period_ns = (period_ticks * 1000000000ULL) / TIM1_CLK_FREQ_HZ;
                    uint64_t next_fire_time = qemu_plugin_get_virtual_timer() + period_ns;

                    snprintf(msg, sizeof(msg), "TIM1: Re-arming timer for next period (%llu ns).", (unsigned long long)period_ns);
                    dev_debug(msg);
                    qemu_plugin_timer_alarm(s->timer_fd, next_fire_time);
                }
             }
            break;
        }

        default:
            snprintf(msg, sizeof(msg), "TIM1: Write to unhandled offset 0x%x", offset);
            dev_debug(msg);
            break;
    }
}