#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "rplidara2.h"

/*
 * Fake RPLidar A2 UART device model
 *
 * Supported commands:
 *   A5 40  RESET
 *   A5 50  GET_DEVICE_INFO
 *   A5 52  GET_DEVICE_HEALTH
 *   A5 20  SCAN
 *   A5 21  FORCE_SCAN (treated same as SCAN)
 *   A5 25  STOP
 *
 * Host usage pattern:
 *   1. call rplidar_init()
 *   2. deliver host UART writes via rplidar_receive()
 *   3. periodically call rplidar_tick(now_us)
 *   4. read outgoing UART bytes with rplidar_read_tx()
 */

#define RPLIDAR_RX_BUF_SIZE 256
#define RPLIDAR_TX_BUF_SIZE 8192

#define RPLIDAR_PREAMBLE               0xA5
#define RPLIDAR_CMD_STOP               0x25
#define RPLIDAR_CMD_SCAN               0x20
#define RPLIDAR_CMD_FORCE_SCAN         0x21
#define RPLIDAR_CMD_RESET              0x40
#define RPLIDAR_CMD_GET_DEVICE_INFO    0x50
#define RPLIDAR_CMD_GET_DEVICE_HEALTH  0x52
#define RPLIDAR_CMD_EXPRESS_SCAN       0x82

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


/* -------------------------------------------------------------------------- */
/* Utility                                                                     */
/* -------------------------------------------------------------------------- */

static size_t min_size(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

static uint16_t clamp_u16_from_int(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 65535) {
        return 65535;
    }
    return (uint16_t)v;
}

/* -------------------------------------------------------------------------- */
/* TX ring buffer helpers                                                      */
/* -------------------------------------------------------------------------- */

static size_t rplidar_tx_available(const fake_rplidar_t *dev)
{
    if (dev->tx_tail >= dev->tx_head) {
        return dev->tx_tail - dev->tx_head;
    }
    return RPLIDAR_TX_BUF_SIZE - dev->tx_head + dev->tx_tail;
}

static size_t rplidar_tx_space(const fake_rplidar_t *dev)
{
    /* keep one byte empty to distinguish full vs empty */
    return (RPLIDAR_TX_BUF_SIZE - 1) - rplidar_tx_available(dev);
}

static bool rplidar_tx_push_byte(fake_rplidar_t *dev, uint8_t b)
{
    if (rplidar_tx_space(dev) == 0) {
        return false;
    }
    dev->tx_buf[dev->tx_tail] = b;
    dev->tx_tail = (dev->tx_tail + 1) % RPLIDAR_TX_BUF_SIZE;
    return true;
}

static size_t rplidar_tx_push_bytes(fake_rplidar_t *dev, const uint8_t *data, size_t len)
{
    size_t pushed = 0;
    while (pushed < len) {
        if (!rplidar_tx_push_byte(dev, data[pushed])) {
            break;
        }
        pushed++;
    }
    return pushed;
}

size_t rplidar_read_tx(fake_rplidar_t *dev, uint8_t *out, size_t max_len)
{
    size_t n = 0;
    while (n < max_len && dev->tx_head != dev->tx_tail) {
        out[n++] = dev->tx_buf[dev->tx_head];
        dev->tx_head = (dev->tx_head + 1) % RPLIDAR_TX_BUF_SIZE;
    }
    return n;
}

/* -------------------------------------------------------------------------- */
/* Protocol builders                                                           */
/* -------------------------------------------------------------------------- */

static void rplidar_make_descriptor(
    uint8_t out[7],
    uint32_t response_length,
    uint8_t send_mode,
    uint8_t data_type)
{
    /*
     * Matches what the ArduPilot driver memcmp's against:
     *   scan:        A5 5A 05 00 00 40 81
     *   device info: A5 5A 14 00 00 00 04
     *   health:      A5 5A 03 00 00 00 06
     */
    out[0] = 0xA5;
    out[1] = 0x5A;
    out[2] = (uint8_t)(response_length & 0xFF);
    out[3] = (uint8_t)((response_length >> 8) & 0xFF);
    out[4] = (uint8_t)((response_length >> 16) & 0xFF);
    out[5] = send_mode;
    out[6] = data_type;
}

static void rplidar_build_device_info_payload(const fake_rplidar_t *dev, uint8_t out[20])
{
    out[0] = dev->model;
    out[1] = dev->firmware_minor;
    out[2] = dev->firmware_major;
    out[3] = dev->hardware;
    memcpy(&out[4], dev->serial_number, 16);
}

static void rplidar_build_health_payload(const fake_rplidar_t *dev, uint8_t out[3])
{
    out[0] = dev->health_status;
    out[1] = (uint8_t)(dev->health_error_code & 0xFF);
    out[2] = (uint8_t)((dev->health_error_code >> 8) & 0xFF);
}

/* -------------------------------------------------------------------------- */
/* Fake scan data                                                              */
/* -------------------------------------------------------------------------- */

static void rplidar_build_scan_sample(fake_rplidar_t *dev, uint8_t out[5])
{
    /*
     * Standard 5-byte scan sample shape expected by the driver:
     *   byte0: quality[7:2], not_startbit[1], startbit[0]
     *   byte1: angle_q6 low 7 bits in [7:1], checkbit in [0]
     *   byte2: angle_q6 high 8 bits
     *   byte3: distance_q2 low 8 bits
     *   byte4: distance_q2 high 8 bits
     */

    double angle_deg = dev->scan_angle_deg;

    /* fake environment */
    double distance_m = 3.0 + 1.5 * sin((angle_deg * 2.0) * M_PI / 180.0);
    if (distance_m < 0.2) {
        distance_m = 0.2;
    }

    uint8_t quality = 15;
    uint8_t startbit = dev->new_scan_flag ? 1 : 0;
    uint8_t not_startbit = startbit ? 0 : 1;
    uint8_t checkbit = 1;

    int angle_q6_i = (int)((fmod(angle_deg, 360.0)) * 64.0);
    if (angle_q6_i < 0) {
        angle_q6_i += (360 * 64);
    }

    int distance_q2_i = (int)(distance_m * 4000.0);

    uint16_t angle_q6 = clamp_u16_from_int(angle_q6_i);
    uint16_t distance_q2 = clamp_u16_from_int(distance_q2_i);

    out[0] = (uint8_t)(((quality & 0x3F) << 2) |
                       ((not_startbit & 0x1) << 1) |
                       (startbit & 0x1));
    out[1] = (uint8_t)(((angle_q6 & 0x7F) << 1) |
                       (checkbit & 0x1));
    out[2] = (uint8_t)((angle_q6 >> 7) & 0xFF);
    out[3] = (uint8_t)(distance_q2 & 0xFF);
    out[4] = (uint8_t)((distance_q2 >> 8) & 0xFF);

    dev->scan_angle_deg += dev->scan_angle_step_deg;
    if (dev->scan_angle_deg >= 360.0) {
        dev->scan_angle_deg -= 360.0;
        dev->new_scan_flag = true;
    } else {
        dev->new_scan_flag = false;
    }
}

/* -------------------------------------------------------------------------- */
/* Command handlers                                                            */
/* -------------------------------------------------------------------------- */

static void rplidar_handle_reset(fake_rplidar_t *dev)
{
    dev->state = RPLIDAR_STATE_RESETTING;
    dev->scan_angle_deg = 0.0;
    dev->new_scan_flag = true;

    rplidar_tx_push_bytes(dev, dev->reset_banner, sizeof(dev->reset_banner));

    dev->state = RPLIDAR_STATE_IDLE;
}

static void rplidar_handle_stop(fake_rplidar_t *dev)
{
    dev->state = RPLIDAR_STATE_IDLE;
}

static void rplidar_handle_get_device_info(fake_rplidar_t *dev)
{
    uint8_t descriptor[7];
    uint8_t payload[20];

    rplidar_make_descriptor(descriptor, 20, 0x00, 0x04);
    rplidar_build_device_info_payload(dev, payload);

    rplidar_tx_push_bytes(dev, descriptor, sizeof(descriptor));
    rplidar_tx_push_bytes(dev, payload, sizeof(payload));
}

static void rplidar_handle_get_device_health(fake_rplidar_t *dev)
{
    uint8_t descriptor[7];
    uint8_t payload[3];

    rplidar_make_descriptor(descriptor, 3, 0x00, 0x06);
    rplidar_build_health_payload(dev, payload);

    rplidar_tx_push_bytes(dev, descriptor, sizeof(descriptor));
    rplidar_tx_push_bytes(dev, payload, sizeof(payload));
}

static void rplidar_handle_scan(fake_rplidar_t *dev, uint64_t now_us)
{
    uint8_t descriptor[7];

    rplidar_make_descriptor(descriptor, 5, 0x40, 0x81);
    rplidar_tx_push_bytes(dev, descriptor, sizeof(descriptor));

    dev->state = RPLIDAR_STATE_STREAMING_SCAN;
    dev->last_sample_time_us = now_us;
}

static void rplidar_handle_command(fake_rplidar_t *dev, uint8_t cmd, uint64_t now_us)
{
    switch (cmd) {
    case RPLIDAR_CMD_RESET:
        rplidar_handle_reset(dev);
        break;
    case RPLIDAR_CMD_STOP:
        rplidar_handle_stop(dev);
        break;
    case RPLIDAR_CMD_GET_DEVICE_INFO:
        rplidar_handle_get_device_info(dev);
        break;
    case RPLIDAR_CMD_GET_DEVICE_HEALTH:
        rplidar_handle_get_device_health(dev);
        break;
    case RPLIDAR_CMD_SCAN:
    case RPLIDAR_CMD_FORCE_SCAN:
        rplidar_handle_scan(dev, now_us);
        break;
    case RPLIDAR_CMD_EXPRESS_SCAN:
        /* not implemented */
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* RX parsing                                                                   */
/* -------------------------------------------------------------------------- */

static void rplidar_process_rx(fake_rplidar_t *dev, uint64_t now_us)
{
    while (dev->rx_len >= 2) {
        /* resync to preamble */
        if (dev->rx_buf[0] != RPLIDAR_PREAMBLE) {
            memmove(&dev->rx_buf[0], &dev->rx_buf[1], dev->rx_len - 1);
            dev->rx_len--;
            continue;
        }

        uint8_t cmd = dev->rx_buf[1];

        switch (cmd) {
        case RPLIDAR_CMD_STOP:
        case RPLIDAR_CMD_SCAN:
        case RPLIDAR_CMD_FORCE_SCAN:
        case RPLIDAR_CMD_RESET:
        case RPLIDAR_CMD_GET_DEVICE_INFO:
        case RPLIDAR_CMD_GET_DEVICE_HEALTH:
            /* 2-byte command */
            memmove(&dev->rx_buf[0], &dev->rx_buf[2], dev->rx_len - 2);
            dev->rx_len -= 2;
            rplidar_handle_command(dev, cmd, now_us);
            break;

        case RPLIDAR_CMD_EXPRESS_SCAN:
            /* Real protocol has payload; ignored for now. Consume 2 bytes only. */
            memmove(&dev->rx_buf[0], &dev->rx_buf[2], dev->rx_len - 2);
            dev->rx_len -= 2;
            rplidar_handle_command(dev, cmd, now_us);
            break;

        default:
            /* unknown command, drop preamble and resync */
            memmove(&dev->rx_buf[0], &dev->rx_buf[1], dev->rx_len - 1);
            dev->rx_len--;
            break;
        }
    }
}

size_t rplidar_receive(fake_rplidar_t *dev, const uint8_t *data, size_t len, uint64_t now_us)
{
    size_t room = RPLIDAR_RX_BUF_SIZE - dev->rx_len;
    size_t n = min_size(room, len);

    if (n > 0) {
        memcpy(&dev->rx_buf[dev->rx_len], data, n);
        dev->rx_len += n;
        rplidar_process_rx(dev, now_us);
    }

    return n;
}

/* -------------------------------------------------------------------------- */
/* Periodic ticking                                                             */
/* -------------------------------------------------------------------------- */

void rplidar_tick(fake_rplidar_t *dev, uint64_t now_us)
{
    if (dev->state != RPLIDAR_STATE_STREAMING_SCAN) {
        return;
    }

    while ((now_us - dev->last_sample_time_us) >= dev->sample_period_us) {
        uint8_t sample[5];
        dev->last_sample_time_us += dev->sample_period_us;
        rplidar_build_scan_sample(dev, sample);
        rplidar_tx_push_bytes(dev, sample, sizeof(sample));
    }
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                               */
/* -------------------------------------------------------------------------- */

void rplidar_init(fake_rplidar_t *dev, double sample_rate_hz)
{
    memset(dev, 0, sizeof(*dev));

    dev->state = RPLIDAR_STATE_IDLE;

    /* fake A2 identity */
    dev->model = 0x28;            /* A2 */
    dev->firmware_minor = 0x00;
    dev->firmware_major = 0x01;
    dev->hardware = 0x10;

    {
        const uint8_t serial[16] = {
            0x12, 0x34, 0x56, 0x78,
            0x9A, 0xBC, 0xDE, 0xF0,
            0x11, 0x22, 0x33, 0x44,
            0x55, 0x66, 0x77, 0x88
        };
        memcpy(dev->serial_number, serial, sizeof(serial));
    }

    dev->health_status = 0x00;
    dev->health_error_code = 0x0000;

    {
        const char *banner = "RPLIDAR A2 RESET BANNER FAKE DATA...........................";
        size_t banner_len = strlen(banner);
        memset(dev->reset_banner, '.', sizeof(dev->reset_banner));
        memcpy(dev->reset_banner, banner, min_size(sizeof(dev->reset_banner), banner_len));
        dev->reset_banner[0] = 'R';  /* make sure driver sees 'R' */
    }

    dev->scan_angle_deg = 0.0;
    dev->scan_angle_step_deg = 1.0;
    dev->new_scan_flag = true;

    if (sample_rate_hz <= 0.0) {
        sample_rate_hz = 400.0;
    }
    dev->sample_period_us = (uint64_t)(1000000.0 / sample_rate_hz);
    dev->last_sample_time_us = 0;
}

/* -------------------------------------------------------------------------- */
/* Example test harness                                                         */
/* -------------------------------------------------------------------------- */

#ifdef RPLIDAR_FAKE_TEST_MAIN

static void dump_hex(const uint8_t *buf, size_t len, const char *label)
{
    size_t i;
    printf("%s (%zu bytes): ", label, len);
    for (i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}

int main(void)
{
    fake_rplidar_t dev;
    uint8_t out[512];
    uint64_t now_us = 0;

    rplidar_init(&dev, 20.0);

    /* RESET */
    {
        const uint8_t cmd[] = {0xA5, 0x40};
        rplidar_receive(&dev, cmd, sizeof(cmd), now_us);
        size_t n = rplidar_read_tx(&dev, out, sizeof(out));
        dump_hex(out, n, "after RESET");
    }

    /* GET_DEVICE_INFO */
    {
        const uint8_t cmd[] = {0xA5, 0x50};
        rplidar_receive(&dev, cmd, sizeof(cmd), now_us);
        size_t n = rplidar_read_tx(&dev, out, sizeof(out));
        dump_hex(out, n, "after GET_DEVICE_INFO");
    }

    /* GET_DEVICE_HEALTH */
    {
        const uint8_t cmd[] = {0xA5, 0x52};
        rplidar_receive(&dev, cmd, sizeof(cmd), now_us);
        size_t n = rplidar_read_tx(&dev, out, sizeof(out));
        dump_hex(out, n, "after GET_DEVICE_HEALTH");
    }

    /* SCAN */
    {
        const uint8_t cmd[] = {0xA5, 0x20};
        rplidar_receive(&dev, cmd, sizeof(cmd), now_us);
        size_t n = rplidar_read_tx(&dev, out, sizeof(out));
        dump_hex(out, n, "after SCAN descriptor");
    }

    /* stream some fake scan data */
    for (int i = 0; i < 5; i++) {
        now_us += 100000; /* 100 ms */
        rplidar_tick(&dev, now_us);
        size_t n = rplidar_read_tx(&dev, out, sizeof(out));
        dump_hex(out, n, "scan burst");
    }

    /* STOP */
    {
        const uint8_t cmd[] = {0xA5, 0x25};
        rplidar_receive(&dev, cmd, sizeof(cmd), now_us);
        size_t n = rplidar_read_tx(&dev, out, sizeof(out));
        dump_hex(out, n, "after STOP");
    }

    return 0;
}

#endif