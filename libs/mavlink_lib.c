#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include "ring_buffer.h"

#define MAVLINK_STX 0xFD

// Example system/component IDs
#define SYS_ID  1
#define COMP_ID 1

// X.25 CRC structure
typedef struct {
    uint16_t crc;
} x25crc_t;

// CRC-16/X.25 functions
static void crc_init(x25crc_t *crc) {
    crc->crc = 0xffff;
}

static void crc_accumulate(x25crc_t *crc, uint8_t data) {
    uint8_t tmp;
    tmp = data ^ (uint8_t)(crc->crc & 0xff);
    tmp ^= tmp << 4;
    crc->crc = (crc->crc >> 8) ^ ((uint16_t)tmp << 8) ^ ((uint16_t)tmp << 3) ^ ((uint16_t)tmp >> 4);
}

static void crc_accumulate_buffer(x25crc_t *crc, const uint8_t *p, size_t len) {
    while (len--) {
        crc_accumulate(crc, *p++);
    }
}

// Builds and sends a MAVLink v2 message
int mav_finalize_message_chan_send(int sockfd,
                                   const struct sockaddr_in *gcs_addr,
                                   uint32_t msgid,
                                   const uint8_t *payload,
                                   uint8_t length,
                                   uint8_t crc_extra,
                                   uint8_t *sequence)
{
    uint8_t header[10];
    uint8_t incompat_flags = 0;
    uint8_t compat_flags = 0;

    header[0] = MAVLINK_STX;
    header[1] = length;
    header[2] = incompat_flags;
    header[3] = compat_flags;
    header[4] = (*sequence) & 0xFF;
    header[5] = SYS_ID;
    header[6] = COMP_ID;
    header[7] = (uint8_t)(msgid & 0xFF);
    header[8] = (uint8_t)((msgid >> 8) & 0xFF);
    header[9] = (uint8_t)((msgid >> 16) & 0xFF);

    (*sequence) = (*sequence + 1) % 256;

    x25crc_t crc;
    crc_init(&crc);
    crc_accumulate_buffer(&crc, &header[1], sizeof(header) - 1); // skip STX
    crc_accumulate_buffer(&crc, payload, length);
    crc_accumulate(&crc, crc_extra);

    size_t packet_len = sizeof(header) + length + 2;
    uint8_t *packet = malloc(packet_len);
    if (!packet) {
        return -1;
    }

    memcpy(packet, header, sizeof(header));
    memcpy(packet + sizeof(header), payload, length);
    packet[sizeof(header) + length] = crc.crc & 0xFF;
    packet[sizeof(header) + length + 1] = (crc.crc >> 8) & 0xFF;

    ssize_t sent = sendto(sockfd, packet, packet_len, 0,
                          (const struct sockaddr *)gcs_addr, sizeof(*gcs_addr));

    if (sent == -1) {
        perror("sendto failed");
        free(packet);
        return -1;
    }

    free(packet);
    // printf("Size of sent packet: %zd bytes\n", sent);
    if (sent != (ssize_t)packet_len) {
        return -1;
    }

    return 0;
}

