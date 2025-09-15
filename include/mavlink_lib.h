/**
 * @file mavlink_lib.h
 * @brief Minimal MAVLink v2 helpers for ArduPilot integration.
 */

#ifndef MAVLINK_LIB_H
#define MAVLINK_LIB_H

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>

/** MAVLink v2 magic byte */
#define MAVLINK_STX 0xFD

/** Default system and component IDs (can be overridden) */
#define SYS_ID  1
#define COMP_ID 1

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

#endif /* MAVLINK_SEND_H */