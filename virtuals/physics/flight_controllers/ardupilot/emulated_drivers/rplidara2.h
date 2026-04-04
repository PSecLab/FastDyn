#ifndef RPLIDAR_EMULATOR_H
#define RPLIDAR_EMULATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "phy.h"

#ifdef __cplusplus
extern "C" {
#endif

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

typedef enum {
    RPLIDAR_DEV_IDLE = 0,
    RPLIDAR_DEV_RESETTING,
    RPLIDAR_DEV_READY,
    RPLIDAR_DEV_SCANNING
} rplidar_mode_t;

typedef struct {
    uint8_t data[RPLIDAR_TX_FIFO_SIZE];
    size_t head;
    size_t tail;
    size_t count;
} rplidar_byte_fifo_t;

#define RPLIDAR_SAMPLE_BATCH 32

typedef struct {
    /* Device identity exposed via DEVICE_INFO response. */
    uint8_t  model;
    uint8_t  firmware_minor;
    uint8_t  firmware_major;
    uint8_t  hardware;
    uint8_t  serialnum[16];

    /* Runtime state. */
    rplidar_mode_t mode;
    bool motor_running;
    bool scan_started;

    /* Lookahead sample buffer, filled by get_lidar_samples(). */
    rplidar_sample_t sample_buf[RPLIDAR_SAMPLE_BATCH];
    size_t sample_buf_count;
    size_t sample_buf_head;

    /* Outbound bytes waiting for rplidar_read(). */
    rplidar_byte_fifo_t tx_fifo;
} rplidar_dev_t;


/* ========================= simulation interface ========================= */

/*
 * Implemented by the simulation environment.
 *
 * Fill up to num_samples entries in the provided array with pre-packed
 * wire-format scan nodes.  Returns the number of samples written, or -1
 * on error.  A return of 0 means no data is available yet; the emulator
 * will retry on the next rplidar_read() call.
 *
 * The caller is responsible for encoding startbit, !startbit, checkbit,
 * angle_q6, and distance_q2 correctly inside each rplidar_sample_t.
 */
// extern int get_lidar_samples(rplidar_sample_t *samples, size_t num_samples);


/* ========================= public API ========================= */

/*
 * Initialise a device instance.  Must be called before any other function.
 * Hardcodes A2M8 identity (model 0x28, firmware 1.24, hardware rev 7).
 */
void rplidar_init(rplidar_dev_t *dev);

/*
 * Returns the number of bytes currently queued and ready to be read.
 */
size_t rplidar_available(rplidar_dev_t *dev);

/**
 * @brief Read up to len bytes from the device TX FIFO into dst.
 * If the device is scanning and the FIFO is running low, new scan nodes
 * are fetched from get_lidar_samples() before the read is serviced.
 * Returns the number of bytes actually copied.
 *
 * @param dev Pointer to the device instance.
 * @param dst Buffer to copy data into.
 * @param len Maximum number of bytes to read.
 * @return Number of bytes read and copied into dst.
 */
size_t rplidar_read(rplidar_dev_t *dev, uint8_t *dst, size_t len);

/*
 * Feed len bytes of host-to-device command data into the emulator.
 * Valid 2-byte commands (preamble 0xA5 + command byte) are acted on
 * immediately; unrecognised commands are silently ignored.
 * Returns the number of bytes consumed.
 */
size_t rplidar_write(rplidar_dev_t *dev, const uint8_t *src, size_t len);


#ifdef __cplusplus
}
#endif

#endif /* RPLIDAR_EMULATOR_H */