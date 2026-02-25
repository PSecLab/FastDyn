#include <device.h>
#include <boardrunner/vio.h>
#include <string.h> // For memset
#include "utils.h"

// --- Forward-declare the DMA API this model depends on ---
void api_dma_request(int stream_id);

// Device Model for ADC3

// Register Offsets
#define ADC_SR_OFFSET    0x00
#define ADC_CR1_OFFSET   0x04
#define ADC_CR2_OFFSET   0x08
#define ADC_DR_OFFSET    0x4C

// Register Bitfields
#define ADC_SR_EOC       (1 << 1)  // End of conversion flag
#define ADC_CR1_EOCIE    (1 << 5)  // Interrupt enable for EOC
#define ADC_CR2_ADON     (1 << 0)  // A/D Converter ON / OFF
#define ADC_CR2_CONT     (1 << 1)  // Continuous conversion mode
#define ADC_CR2_SWSTART  (1 << 30) // Start conversion of regular channels

// The ADC IRQ number
#define ADC_IRQ 18 // NOTE: The common ADC IRQ for ADC1/2/3 is 18, not 72.

typedef struct {
    uint32_t sr;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t dr;
    uint32_t smpr1, smpr2, sqr1, sqr2, sqr3;
    uint64_t conversion_timer;
} ADC3State;

static ADC3State adc3_state;

/**
 * @brief Callback for the periodic conversion timer.
 * This is the correct place to model all "End of Conversion" events.
 */
static void adc_periodic_conversion_cb(void *opaque) {
    ADC3State *s = (ADC3State *)opaque;

    if ((s->cr2 & ADC_CR2_ADON) && (s->cr2 & ADC_CR2_CONT)) {
        dev_debug("ADC3: Continuous conversion finished.\n");
        // 1. Set the End of Conversion flag in the status register.
        s->sr |= ADC_SR_EOC;
        s->dr = 0x0AAA; // Provide a new data value.

        // 2. (FIXED) Request a DMA transfer, as the hardware would.
        const int connected_dma_stream = 8; // DMA2, Stream 0
        api_dma_request(connected_dma_stream);

        // 3. (FIXED) Fire the ADC interrupt ONLY if it's enabled.
        if (s->cr1 & ADC_CR1_EOCIE) {
            dev_debug("ADC3: Firing EOC interrupt.\n");
            qemu_plugin_raise_irq(ADC_IRQ, false);
        }
    }
}

/**
 * @brief Initializes the ADC3 device model state.
 */
void adc3_init(ConfigSection* model_info) {
    memset(&adc3_state, 0, sizeof(adc3_state));

    // Create a periodic timer for continuous conversion mode.
    // A period of 15us is too fast and causes performance issues.
    // A 1ms period is much more efficient and still satisfies the firmware's logic.
    const uint64_t conversion_period_ns = 20000000; // CORRECTED: Was 15000, now 1ms

    adc3_state.conversion_timer = qemu_plugin_timer_new_period_ns(
        adc_periodic_conversion_cb, &adc3_state, conversion_period_ns
    );

    dev_debug("ADC3: Device model initialized.\n");
}

uint64_t adc3_read(void *opaque, hwaddr addr, unsigned size) {
    uint32_t offset = addr - 0x40012200;
    uint32_t ret_val = 0;
    // (FIXED) Removed interrupt call from here.

    switch (offset) {
        case ADC_SR_OFFSET:
            ret_val = adc3_state.sr;
            break;
        case ADC_CR1_OFFSET:
            ret_val = adc3_state.cr1;
            break;
        case ADC_CR2_OFFSET:
            ret_val = adc3_state.cr2;
            break;
        case ADC_DR_OFFSET:
            // Reading DR automatically clears the EOC flag.
            adc3_state.sr &= ~ADC_SR_EOC;
            ret_val = adc3_state.dr;
            // (FIXED) Removed api_dma_request() call from here.
            break;
        case 0x0C: ret_val = adc3_state.smpr1; break;
        case 0x10: ret_val = adc3_state.smpr2; break;
        case 0x2C: ret_val = adc3_state.sqr1; break;
        case 0x30: ret_val = adc3_state.sqr2; break;
        case 0x34: ret_val = adc3_state.sqr3; break;
        default:
            // It's better to avoid debug prints for unhandled reads
            // as they can be noisy.
            break;
    }
    return ret_val;
}

void adc3_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    uint32_t offset = addr - 0x40012200;
    // (FIXED) Removed interrupt call from here.

    switch (offset) {
        case ADC_SR_OFFSET:
            break; // SR is mostly read-only
        case ADC_CR1_OFFSET:
            adc3_state.cr1 = value;
            break;
        case ADC_CR2_OFFSET:
            if ((value & ADC_CR2_SWSTART) && (value & ADC_CR2_ADON)) {
                dev_debug("ADC3: Software conversion triggered.\n");
                // Immediately fire one conversion to kick things off.
                adc_periodic_conversion_cb(&adc3_state);
            }
            // Update state. The SWSTART bit is self-clearing in hardware.
            adc3_state.cr2 = value & ~ADC_CR2_SWSTART;
            break;
        case 0x0C: adc3_state.smpr1 = value; break;
        case 0x10: adc3_state.smpr2 = value; break;
        case 0x2C: adc3_state.sqr1 = value; break;
        case 0x30: adc3_state.sqr2 = value; break;
        case 0x34: adc3_state.sqr3 = value; break;
        default:
            break;
    }
}