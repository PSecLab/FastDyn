#include <device.h>
#include <devmodels_apis.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Device Model for USART1

// Inferred Register Addresses
#define USART1_SR       0x40011000
#define USART1_DR       0x40011004
#define USART1_BRR      0x40011008
#define USART1_CR1      0x4001100C

// Inferred Status Register (SR) Bits from STM32F4 Reference Manual
#define SR_RXNE         (1 << 5) // Read data register not empty
#define SR_TC           (1 << 6) // Transmission complete
#define SR_TXE          (1 << 7) // Transmit data register empty
#define SR_IDLE         (1 << 4) // Idle line detected

// State structure for the emulated USART1 device
typedef struct {
    uint32_t sr;
    uint32_t dr;
    uint32_t brr;
    uint32_t cr1;
    int pty_fd;
    uint64_t rx_timer;      // Periodic timer to check for new data
    uint64_t idle_timer;    // One-shot timer to set the IDLE flag
    uint8_t rx_buf;
    bool rx_buf_full;
} USART1State;

// A single, global static state for our device.
static USART1State usart1_state;

// One-shot callback to set the IDLE flag a moment after a character is received.
static void usart1_set_idle(void *opaque) {
    USART1State *s = &usart1_state;
    s->sr |= SR_IDLE;
    dev_debug("USART1: Idle timer fired, IDLE bit set.");
}

// Periodic callback to check for received characters from the PTY
static void usart1_check_rx(void *opaque) {
    USART1State *s = &usart1_state;
    if (s->pty_fd < 0 || s->rx_buf_full) {
        return;
    }

    uint8_t ch;
    int ret = api_pty_read_nonblock(s->pty_fd, &ch);
    if (ret == 1) { // A byte was successfully read
        s->rx_buf = ch;
        s->rx_buf_full = true;

        // Set RXNE immediately.
        s->sr |= SR_RXNE;
        dev_debug("USART1: Byte received, RXNE set.");

        // Arm a one-shot timer with a realistic delay (~1 character time).
        // A 100 microsecond delay allows for both the "fast" and "slow" poll behaviors.
        uint64_t current_time = qemu_plugin_get_virtual_timer();
        qemu_plugin_timer_alarm(s->idle_timer, current_time + 100000); // 100 us delay
    }
}

// This function will emulate all device reads
uint64_t usart1_read(void *opaque, hwaddr addr, unsigned size) {
    USART1State *s = &usart1_state;
    uint64_t value_to_return = 0;

    switch (addr) {
        case USART1_SR:
            value_to_return = s->sr;
            break;
        case USART1_DR:
            // The STM32 manual states the IDLE flag is cleared by an SR read
            // followed by a DR read. This DR read serves that purpose.
            s->sr &= ~SR_IDLE;

            if (s->rx_buf_full) {
                value_to_return = s->rx_buf;
                s->rx_buf_full = false;
                // Reading the data buffer also clears the RXNE flag.
                s->sr &= ~SR_RXNE;
            }
            break;
        default:
            dev_debug("USART1: Unhandled read");
            break;
    }
    return value_to_return;
}

// This function will emulate all device writes
void usart1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    USART1State *s = &usart1_state;

    switch (addr) {
        case USART1_DR:
            if (s->pty_fd >= 0) {
                uint8_t ch = value & 0xFF;
                api_pty_write_req(s->pty_fd, ch);
            }
            break;
        case USART1_BRR:
            s->brr = value;
            break;
        case USART1_CR1:
            s->cr1 = value;
            break;
        default:
            dev_debug("USART1: Unhandled write");
            break;
    }
}

void usart1_init(void *opaque) {
    USART1State *s = &usart1_state;
    memset(s, 0, sizeof(USART1State));

    // Initialize SR to the idle transmit state 0xC0.
    s->sr = SR_TXE | SR_TC;

    s->pty_fd = api_pty_fd_gen();

    if (s->pty_fd < 0) {
        dev_debug("USART1-ERROR: Failed to open PTY. Is the host command running?");
    } else {
        dev_debug("USART1: Successfully opened PTY device.");
        // Create a periodic timer to poll for incoming characters.
        s->rx_timer = qemu_plugin_timer_new_period_ns(usart1_check_rx, NULL, 2000000); // Check every 2ms
        // Create the one-shot timer that will be armed when data arrives.
        s->idle_timer = qemu_plugin_timer_new_ns(usart1_set_idle, NULL);
    }
}