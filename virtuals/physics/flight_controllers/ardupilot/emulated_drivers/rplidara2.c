/*
 * Minimal stateful RPLidar A2 emulator for ArduPilot-style UART traffic.
 *
 * Device-side model:
 *   - host writes commands into rplidar_write()
 *   - host reads queued device response bytes from rplidar_read()
 *
 * Scan data is sourced from get_lidar_samples(), which must be provided
 * by the surrounding simulation environment.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "rplidara2.h"
#include "phy.h"

#define RPLIDAR_PREAMBLE              0xA5
#define RPLIDAR_CMD_STOP              0x25
#define RPLIDAR_CMD_SCAN              0x20
#define RPLIDAR_CMD_FORCE_SCAN        0x21
#define RPLIDAR_CMD_RESET             0x40
#define RPLIDAR_CMD_GET_DEVICE_INFO   0x50
#define RPLIDAR_CMD_GET_DEVICE_HEALTH 0x52

#define RPLIDAR_RESP_SYNC_1           0xA5
#define RPLIDAR_RESP_SYNC_2           0x5A

#define RPLIDAR_SCAN_DESCRIPTOR_TYPE        0x81
#define RPLIDAR_HEALTH_DESCRIPTOR_TYPE      0x06
#define RPLIDAR_DEVICE_INFO_DESCRIPTOR_TYPE 0x04

#define RPLIDAR_TX_FIFO_SIZE 8192

/*
 * Pre-packed scan sample as returned by get_lidar_samples().
 * Bytes are already in the wire format expected by the ArduPilot driver,
 * so they can be written into the TX FIFO without further encoding.
 *
 *   sync_quality : bit0 = startbit, bit1 = !startbit, bits[7:2] = quality
 *   angle_lsb    : bit0 = checkbit (must be 1), bits[7:1] = angle_q6[6:0]
 *   angle_msb    : angle_q6[14:7]
 *   dist_lsb     : distance_q2[7:0]
 *   dist_msb     : distance_q2[15:8]
 */
// typedef struct {
//     uint8_t sync_quality;
//     uint8_t angle_lsb;
//     uint8_t angle_msb;
//     uint8_t dist_lsb;
//     uint8_t dist_msb;
// } rplidar_sample_t;

/*
 * Implemented by the simulation environment.
 * Fills up to num_samples entries; returns the number actually written,
 * or -1 on error.  Callers must handle a return of 0 (no data yet)
 * gracefully.
 */
// extern int get_lidar_samples(rplidar_sample_t *samples, size_t num_samples);

/* Batch size used when refilling the internal sample buffer. */
#define RPLIDAR_SAMPLE_BATCH 32

// typedef struct {
//     /* Device identity exposed via DEVICE_INFO payload. */
//     uint8_t  model;            /* 0x28 = A2M8                        */
//     uint8_t  firmware_minor;   /* reported as fw version minor       */
//     uint8_t  firmware_major;   /* reported as fw version major       */
//     uint8_t  hardware;         /* hardware revision                  */
//     uint8_t  serialnum[16];

//     /* Runtime state. */
//     rplidar_mode_t mode;
//     bool motor_running;
//     bool scan_started;

//     /*
//      * Small lookahead buffer: filled by get_lidar_samples(), drained one
//      * entry at a time by queue_scan_node().  Keeps the number of calls to
//      * get_lidar_samples() low and lets callers return a batch efficiently.
//      */
//     rplidar_sample_t sample_buf[RPLIDAR_SAMPLE_BATCH];
//     size_t sample_buf_count;   /* valid entries waiting in sample_buf */
//     size_t sample_buf_head;    /* next entry to consume               */

//     /* Outbound bytes waiting for rplidar_read(). */
//     rplidar_byte_fifo_t tx_fifo;
// } rplidar_dev_t;

/* ========================= FIFO helpers ========================= */

static void fifo_init(rplidar_byte_fifo_t *f)
{
    memset(f, 0, sizeof(*f));
}

static size_t fifo_write(rplidar_byte_fifo_t *f, const uint8_t *src, size_t len)
{
    size_t written = 0;
    while (written < len && f->count < RPLIDAR_TX_FIFO_SIZE) {
        f->data[f->tail] = src[written];
        f->tail = (f->tail + 1) % RPLIDAR_TX_FIFO_SIZE;
        f->count++;
        written++;
    }
    return written;
}

static size_t fifo_read(rplidar_byte_fifo_t *f, uint8_t *dst, size_t len)
{
    size_t n = 0;
    while (n < len && f->count > 0) {
        dst[n] = f->data[f->head];
        f->head = (f->head + 1) % RPLIDAR_TX_FIFO_SIZE;
        f->count--;
        n++;
    }
    return n;
}

static size_t fifo_available(const rplidar_byte_fifo_t *f)
{
    return f->count;
}


/* ========================= protocol helpers ========================= */

static void queue_bytes(rplidar_dev_t *dev, const uint8_t *data, size_t len)
{
    fifo_write(&dev->tx_fifo, data, len);
}

static void queue_descriptor(rplidar_dev_t *dev,
                              uint32_t payload_len,
                              bool     single_response,
                              uint8_t  data_type)
{
    uint8_t d[7];
    d[0] = RPLIDAR_RESP_SYNC_1;
    d[1] = RPLIDAR_RESP_SYNC_2;
    d[2] = (uint8_t)(payload_len        & 0xFF);
    d[3] = (uint8_t)((payload_len >>  8) & 0xFF);
    d[4] = (uint8_t)((payload_len >> 16) & 0xFF);
    d[5] = (uint8_t)((payload_len >> 24) & 0x3F);
    if (!single_response) {
        d[5] |= 0x40;   /* multiple-response / streaming */
    }
    d[6] = data_type;
    queue_bytes(dev, d, sizeof(d));
}

static void queue_device_info(rplidar_dev_t *dev)
{
    queue_descriptor(dev, 0x14, true, RPLIDAR_DEVICE_INFO_DESCRIPTOR_TYPE);

    uint8_t p[20];
    memset(p, 0, sizeof(p));
    p[0] = dev->model;
    p[1] = dev->firmware_minor;
    p[2] = dev->firmware_major;
    p[3] = dev->hardware;
    memcpy(&p[4], dev->serialnum, 16);
    queue_bytes(dev, p, sizeof(p));
}

static void queue_health(rplidar_dev_t *dev, uint8_t status, uint16_t error_code)
{
    queue_descriptor(dev, 0x03, true, RPLIDAR_HEALTH_DESCRIPTOR_TYPE);

    uint8_t p[3];
    p[0] = status;
    p[1] = (uint8_t)(error_code        & 0xFF);
    p[2] = (uint8_t)((error_code >> 8) & 0xFF);
    queue_bytes(dev, p, sizeof(p));
}

static void queue_scan_descriptor(rplidar_dev_t *dev)
{
    /* streaming response, 5 bytes per node */
    queue_descriptor(dev, 0x05, false, RPLIDAR_SCAN_DESCRIPTOR_TYPE);
}

static void queue_reset_banner(rplidar_dev_t *dev)
{
    /*
     * The ArduPilot driver RESET handler:
     *   1. scans incoming bytes until it sees 'R' (0x52)
     *   2. then reads 63 bytes total (including the 'R') and discards them
     *
     * Any 63-byte sequence whose first byte is 'R' satisfies the protocol.
     */
    uint8_t banner[63];
    memset(banner, 0x00, sizeof(banner));

    const char msg[] = "RPlidar boot v1.24 (A2M8) READY.           "
                       "                   ";   /* pad to 63 bytes  */
    /* msg[] is exactly 63 chars including the NUL, copy without NUL */
    memcpy(banner, msg, sizeof(banner));

    queue_bytes(dev, banner, sizeof(banner));
}


/* ========================= scan node feeding ========================= */

/*
 * Attempt to fetch a fresh batch of samples from the simulation into the
 * device's lookahead buffer.  Returns true if at least one sample is now
 * available.
 */
static bool refill_sample_buf(rplidar_dev_t *dev)
{
    if (dev->sample_buf_count > 0) {
        return true;   /* still have unconsumed samples */
    }

    int got = phy_get_lidar_samples(dev->sample_buf, RPLIDAR_SAMPLE_BATCH);
    if (got <= 0) {
        return false;  /* simulation not ready yet, try later */
    }

    dev->sample_buf_count = (size_t)got;
    dev->sample_buf_head  = 0;
    return true;
}

/*
 * Push one pre-packed scan node into the TX FIFO.
 * Returns false if no sample data was available from the simulation.
 */
static bool queue_scan_node(rplidar_dev_t *dev)
{
    if (!refill_sample_buf(dev)) {
        return false;
    }

    const rplidar_sample_t *s = &dev->sample_buf[dev->sample_buf_head];

    /*
     * Wire layout (5 bytes, already encoded in rplidar_sample_t):
     *
     *   byte 0  sync_quality : bits[1:0] = S/!S flags, bits[7:2] = quality
     *   byte 1  angle_lsb    : bit0 = checkbit(1), bits[7:1] = angle_q6[6:0]
     *   byte 2  angle_msb    : angle_q6[14:7]
     *   byte 3  dist_lsb     : distance_q2[7:0]
     *   byte 4  dist_msb     : distance_q2[15:8]
     *
     * get_lidar_samples() is responsible for setting these correctly,
     * including the startbit and checkbit fields.
     */
    uint8_t wire[5];
    wire[0] = s->sync_quality;
    wire[1] = s->angle_lsb;
    wire[2] = s->angle_msb;
    wire[3] = s->dist_lsb;
    wire[4] = s->dist_msb;

    queue_bytes(dev, wire, sizeof(wire));

    dev->sample_buf_head++;
    dev->sample_buf_count--;
    return true;
}

/*
 * Prime the TX FIFO with up to `count` scan nodes at startup or during
 * refill.  Stops early if the simulation has no data yet.
 */
static void prime_scan_nodes(rplidar_dev_t *dev, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!queue_scan_node(dev)) {
            break;
        }
    }
}


/* ========================= public API ========================= */

void rplidar_init(rplidar_dev_t *dev)
{
    memset(dev, 0, sizeof(*dev));
    fifo_init(&dev->tx_fifo);

    /* RPLidar A2M8 realistic identity values */
    dev->model          = 0x28;   /* A2M8                         */
    dev->firmware_minor = 24;     /* firmware 1.24                */
    dev->firmware_major = 1;
    dev->hardware       = 7;      /* hardware revision 7          */

    /* Plausible fixed serial number (16 bytes, hex-printable range) */
    const uint8_t sn[16] = {
        0x3A, 0xF2, 0x11, 0x04,
        0xC7, 0x8B, 0x0E, 0x55,
        0x29, 0xD0, 0x6A, 0x3C,
        0xB1, 0x47, 0x90, 0xFE
    };
    memcpy(dev->serialnum, sn, sizeof(sn));

    dev->mode          = RPLIDAR_DEV_IDLE;
    dev->motor_running = false;
    dev->scan_started  = false;

    dev->sample_buf_count = 0;
    dev->sample_buf_head  = 0;
}

size_t rplidar_available(const rplidar_dev_t *dev)
{
    return fifo_available(&dev->tx_fifo);
}

size_t rplidar_read(rplidar_dev_t *dev, uint8_t *dst, size_t len)
{
    /*
     * If we are scanning and the TX FIFO is running low, pull more samples
     * from the simulation before handing bytes to the caller.  The threshold
     * (128 bytes = ~25 nodes) gives enough headroom to avoid underruns at
     * typical UART baud rates.
     */
    if (dev->mode == RPLIDAR_DEV_SCANNING && fifo_available(&dev->tx_fifo) < 128) {
        prime_scan_nodes(dev, RPLIDAR_SAMPLE_BATCH);
    }

    return fifo_read(&dev->tx_fifo, dst, len);
}

size_t rplidar_write(rplidar_dev_t *dev, const uint8_t *src, size_t len)
{
    size_t consumed = 0;

    while (consumed + 2 <= len) {
        if (src[consumed] != RPLIDAR_PREAMBLE) {
            consumed++;
            continue;
        }

        uint8_t cmd = src[consumed + 1];
        consumed += 2;

        switch (cmd) {

        case RPLIDAR_CMD_RESET:
            dev->mode             = RPLIDAR_DEV_RESETTING;
            dev->motor_running    = false;
            dev->scan_started     = false;
            dev->sample_buf_count = 0;
            dev->sample_buf_head  = 0;
            queue_reset_banner(dev);
            break;

        case RPLIDAR_CMD_GET_DEVICE_INFO:
            dev->mode = RPLIDAR_DEV_READY;
            queue_device_info(dev);
            break;

        case RPLIDAR_CMD_GET_DEVICE_HEALTH:
            queue_health(dev, 0x00, 0x0000);   /* status OK, no error */
            break;

        case RPLIDAR_CMD_SCAN:
        case RPLIDAR_CMD_FORCE_SCAN:
            dev->mode             = RPLIDAR_DEV_SCANNING;
            dev->motor_running    = true;
            dev->scan_started     = true;
            dev->sample_buf_count = 0;
            dev->sample_buf_head  = 0;
            queue_scan_descriptor(dev);
            prime_scan_nodes(dev, RPLIDAR_SAMPLE_BATCH);
            break;

        case RPLIDAR_CMD_STOP:
            dev->mode          = RPLIDAR_DEV_READY;
            dev->scan_started  = false;
            dev->motor_running = false;
            break;

        default:
            /* Silently ignore unrecognised commands. */
            break;
        }
    }

    return consumed;
}