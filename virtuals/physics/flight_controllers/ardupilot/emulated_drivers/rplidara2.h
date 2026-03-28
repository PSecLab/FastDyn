#ifndef FAKE_RPLIDAR_H
#define FAKE_RPLIDAR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// #define RPLIDAR_RX_BUF_SIZE 256
// #define RPLIDAR_TX_BUF_SIZE 8192

// #define RPLIDAR_PREAMBLE               0xA5
// #define RPLIDAR_CMD_STOP               0x25
// #define RPLIDAR_CMD_SCAN               0x20
// #define RPLIDAR_CMD_FORCE_SCAN         0x21
// #define RPLIDAR_CMD_RESET              0x40
// #define RPLIDAR_CMD_GET_DEVICE_INFO    0x50
// #define RPLIDAR_CMD_GET_DEVICE_HEALTH  0x52
// #define RPLIDAR_CMD_EXPRESS_SCAN       0x82

typedef enum {
    RPLIDAR_STATE_IDLE = 0,
    RPLIDAR_STATE_STREAMING_SCAN,
    RPLIDAR_STATE_RESETTING
} rplidar_state_t;

typedef struct {
    /* RX assembly buffer (host -> device) */
    uint8_t rx_buf[RPLIDAR_RX_BUF_SIZE];
    size_t rx_len;

    /* TX queue (device -> host) */
    uint8_t tx_buf[RPLIDAR_TX_BUF_SIZE];
    size_t tx_head;   /* next byte to read */
    size_t tx_tail;   /* next position to write */

    rplidar_state_t state;

    /* fake identity */
    uint8_t model;
    uint8_t firmware_minor;
    uint8_t firmware_major;
    uint8_t hardware;
    uint8_t serial_number[16];

    /* fake health */
    uint8_t health_status;
    uint16_t health_error_code;

    /* fake reset banner */
    uint8_t reset_banner[63];

    /* scan generation */
    double scan_angle_deg;
    double scan_angle_step_deg;
    bool new_scan_flag;

    uint64_t sample_period_us;
    uint64_t last_sample_time_us;
} fake_rplidar_t;

/*
 * Initialize the fake device.
 *
 * sample_rate_hz:
 *   Number of fake scan samples per second generated while in scan mode.
 *   If <= 0, the implementation may fall back to a default rate.
 */
void rplidar_init(fake_rplidar_t *dev, double sample_rate_hz);

/*
 * Deliver bytes written by the host to the lidar UART.
 *
 * data/len:
 *   Incoming UART bytes from the flight controller / driver.
 *
 * now_us:
 *   Current emulated time in microseconds.
 *
 * returns:
 *   Number of bytes consumed from data.
 */
size_t rplidar_receive(fake_rplidar_t *dev,
                       const uint8_t *data,
                       size_t len,
                       uint64_t now_us);

/*
 * Advance the device model and emit scan samples if scan mode is active.
 *
 * now_us:
 *   Current emulated time in microseconds.
 */
void rplidar_tick(fake_rplidar_t *dev, uint64_t now_us);

/*
 * Read bytes the fake lidar has transmitted.
 *
 * out/max_len:
 *   Destination buffer for outgoing UART bytes.
 *
 * returns:
 *   Number of bytes copied into out.
 */
size_t rplidar_read_tx(fake_rplidar_t *dev, uint8_t *out, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_RPLIDAR_H */