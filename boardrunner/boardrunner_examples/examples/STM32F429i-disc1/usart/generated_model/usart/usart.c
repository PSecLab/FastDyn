#include <device.h>
#include <boardrunner/vio.h>
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

// State structure for the emulated USART1 device
typedef struct {
    uint32_t sr;
    uint32_t dr;
    uint32_t brr;
    uint32_t cr1;
    int pty_fd;
    uint64_t rx_timer;
    uint8_t rx_buf;
    bool rx_buf_full;
} USART1State;

// A single, global static state for our device to prevent segfaults.
static USART1State usart1_state;

// Timer callback to check for received characters from the PTY
static void usart1_check_rx(void *opaque) {
    // This function also uses the reliable global state.
    USART1State *s = &usart1_state;
    if (s->pty_fd < 0 || s->rx_buf_full) {
        return;
    }

    uint8_t ch;
    int ret = api_pty_read_nonblock(s->pty_fd, &ch);
    if (ret == 1) { // A byte was successfully read
        s->rx_buf = ch;
        s->rx_buf_full = true;
        s->sr |= SR_RXNE; // Set the RXNE flag to signal data is ready
        dev_debug("USART1: Byte received from PTY, RXNE set.");
    }
}

// This function will emulate all device reads
uint64_t usart1_read(void *opaque, hwaddr addr, unsigned size) {
    // We ignore the passed 'opaque' pointer and use our reliable global state.
    USART1State *s = &usart1_state;
    uint64_t value = 0;

    switch (addr) {
        case USART1_SR:
            value = s->sr;
            break;
        case USART1_DR:
            if (s->rx_buf_full) {
                value = s->rx_buf;
                s->rx_buf_full = false;
                s->sr &= ~SR_RXNE; // Reading DR clears the RXNE flag
                dev_debug("USART1: Guest read DR, clearing RXNE.");
            }
            break;
        default:
            dev_debug("USART1: Unhandled read");
            break;
    }
    return value;
}

// This function will emulate all device writes
void usart1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    // We ignore the passed 'opaque' pointer and use our reliable global state.
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
    // We ignore the passed 'opaque' pointer and initialize our reliable global state.
    USART1State *s = &usart1_state;
    memset(s, 0, sizeof(USART1State));

    // CRITICAL: Initialize the Status Register to the idle state observed in traces.
    s->sr = SR_TXE | SR_TC; // Should be 0xC0

    s->pty_fd = api_pty_fd_gen();

    if (s->pty_fd < 0) {
        dev_debug("USART1-ERROR: Failed to open PTY. Is the host command running?");
    } else {
        dev_debug("USART1: Successfully opened PTY device.");
        // The timer will call usart1_check_rx, we can pass NULL as user data
        // since the callback also uses the global state directly.
        s->rx_timer = qemu_plugin_timer_new_period_ns(usart1_check_rx, NULL, 20000000); // Check every 20ms
    }
}