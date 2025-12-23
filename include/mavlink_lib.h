/**
 * @file mavlink_lib.h
 * @brief Minimal MAVLink v2 helpers for ArduPilot integration.
 */

#ifndef MAVLINK_LIB_H
#define MAVLINK_LIB_H

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>
#include "ring_buffer.h"

/** MAVLink v2 magic byte */
#define MAVLINK_STX 0xFD

/** Default system and component IDs (can be overridden) */
#define SYS_ID  1
#define COMP_ID 1

/**
 * @brief Wrapper around mav_finalize_message_chan_send to send MAVLink messages over UDP.
 *
 * This function supplies the sockfd and gcs_addr parameters to mav_finalize_message_chan_send.
 *
 * @param message_id MAVLink message ID (24-bit).
 * @param payload Pointer to payload data buffer.
 * @param length Length of payload in bytes.
 * @param crc_extra CRC extra byte specific to this message ID.
 * @param sequence Pointer to sequence counter (increments after send).
 * @return 0 on success, -1 on failure.
 */
int send_mavlink_payload(uint32_t message_id,
                         const uint8_t *payload,
                         uint8_t length,
                         uint8_t crc_extra,
                         uint8_t *sequence);

/**
 * @brief Send a MAVLink v2 message over UDP.
 *
 * This function constructs a MAVLink v2 packet (header + payload + CRC)
 * and sends it to the given ground control station (GCS) address.
 *
 * @param sockfd     UDP socket file descriptor.
 * @param gcs_addr   Destination GCS address (IPv4/UDP).
 * @param msgid      MAVLink message ID (24-bit).
 * @param payload    Pointer to payload data buffer.
 * @param length     Length of payload in bytes.
 * @param crc_extra  CRC extra byte specific to this message ID.
 * @param sequence   Pointer to sequence counter (increments after send).
 * @return 0 on success, -1 on failure.
 */
int mav_finalize_message_chan_send(int sockfd,
                                   const struct sockaddr_in *gcs_addr,
                                   uint32_t msgid,
                                   const uint8_t *payload,
                                   uint8_t length,
                                   uint8_t crc_extra,
                                   uint8_t *sequence);

/**
 * @brief Start the GCS listener thread if not already running.
 *
 * @param rb Pointer to ring buffer for incoming data.
 */
void start_gcs_listener(RingBuffer *rb);

/**
* @brief Mark the open of a flight log file descriptor.
*
* @param fd File descriptor of the flight log file.
*/
void mark_open_flight_log_fd(int fd);

/**
* @brief Mark the close of a flight log file descriptor.
*
* @param fd File descriptor of the flight log file.
*/
void mark_close_flight_log_fd(int fd);

/**
 * @brief Check how many bytes are available in the ring buffer.
 *
 * @param rb Pointer to ring buffer.
 * @return Number of bytes available to read.
 */
size_t bytes_available(RingBuffer *rb);

/**
 * @brief Read a byte from the ring buffer.
 *
 * @param rb Pointer to ring buffer.
 * @param byte Pointer to store the read byte.
 */
void read_byte(RingBuffer *rb, unsigned char *byte);

typedef struct {
    double latitude_deg;
    double longitude_deg;
    double altitude_m;       // meters
    float velocity_n;        // north velocity (m/s)
    float velocity_e;        // east velocity (m/s)
    float velocity_d;        // down velocity (m/s, NED frame)
    uint32_t timestamp_sec;  // seconds
    uint32_t timestamp_nsec; // nanoseconds
    uint8_t fix_type;        // 0=no fix, 3=3D fix
    uint8_t satellites_visible;
    float yaw_deg;        // yaw in degrees
} gps_input_t;

void send_mavlink_gps_input(uint8_t system_id, uint8_t component_id, const gps_input_t *gps);

#endif /* MAVLINK_LIB_H */