#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include "ring_buffer.h"
#include "mavlink.h"

#ifndef MAVLINK_STX
#define MAVLINK_STX 0xFD
#endif
// Example system/component IDs
#define SYS_ID  1
#define COMP_ID 1

#define GCS_SYS_ID 255
#define GCS_COMP_ID 190
#define DEFAULT_FASTDYN_MAVLINK_FIRMWARE_PORT 14551

int fuzzer_sockfd = -1;
struct sockaddr_in fuzzer_addr;

static int fastdyn_mavlink_firmware_port(void) {
    static int cached_port = 0;
    if (cached_port > 0) {
        return cached_port;
    }

    const char *env = getenv("FASTDYN_MAVLINK_FIRMWARE_PORT");
    if (env != NULL && env[0] != '\0') {
        char *end = NULL;
        errno = 0;
        long value = strtol(env, &end, 10);
        if (errno == 0 && end != env && *end == '\0' && value > 0 && value <= 65535) {
            cached_port = (int)value;
            return cached_port;
        }
        fprintf(stderr, "Invalid FASTDYN_MAVLINK_FIRMWARE_PORT=%s, using %d\n",
                env, DEFAULT_FASTDYN_MAVLINK_FIRMWARE_PORT);
    }

    cached_port = DEFAULT_FASTDYN_MAVLINK_FIRMWARE_PORT;
    return cached_port;
}

// X.25 CRC structure
typedef struct {
    uint16_t crc;
} x25crc_t;

// CRC-16/X.25 functions
static void crc_init_custom(x25crc_t *crc) {
    crc->crc = 0xffff;
}

static void crc_accumulate_custom(x25crc_t *crc, uint8_t data) {
    uint8_t tmp;
    tmp = data ^ (uint8_t)(crc->crc & 0xff);
    tmp ^= tmp << 4;
    crc->crc = (crc->crc >> 8) ^ ((uint16_t)tmp << 8) ^ ((uint16_t)tmp << 3) ^ ((uint16_t)tmp >> 4);
}

static void crc_accumulate_buffer_custom(x25crc_t *crc, const uint8_t *p, size_t len) {
    while (len--) {
        crc_accumulate_custom(crc, *p++);
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
    crc_init_custom(&crc);
    crc_accumulate_buffer_custom(&crc, &header[1], sizeof(header) - 1); // skip STX
    crc_accumulate_buffer_custom(&crc, payload, length);
    crc_accumulate_custom(&crc, crc_extra);

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

static bool initialize_fuzzer_socket() {
    if (fuzzer_sockfd != -1) {
        return true; // already initialized
    }

    fuzzer_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fuzzer_sockfd < 0) {
        perror("Fuzzer socket creation failed");
        return false;
    }

    memset(&fuzzer_addr, 0, sizeof(fuzzer_addr));
    fuzzer_addr.sin_family = AF_INET;
    fuzzer_addr.sin_port = htons(fastdyn_mavlink_firmware_port());
    fuzzer_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    return true;
}



int create_fuzzed_mavlink_packet(uint32_t msgid,
                                uint8_t crc_extra,
                                const uint8_t *payload,
                                uint8_t length,
                                uint8_t real_length,
                                uint8_t sysid,
                                uint8_t compid)
{
    if (fuzzer_sockfd == -1) {
        if (!initialize_fuzzer_socket()) {
            return -1;
        }
    }

    int sockfd = fuzzer_sockfd;
    struct sockaddr_in *recv_addr = &fuzzer_addr;

    static uint8_t seq_number = 0;

    uint8_t header[10];
    uint8_t incompat_flags = 0; // in future do this
    uint8_t compat_flags = 0;

    header[0] = MAVLINK_STX;
    header[1] = length;
    header[2] = incompat_flags;
    header[3] = compat_flags;
    header[4] = seq_number & 0xFF;
    header[5] = sysid;
    header[6] = compid;
    header[7] = (uint8_t)(msgid & 0xFF);
    header[8] = (uint8_t)((msgid >> 8) & 0xFF);
    header[9] = (uint8_t)((msgid >> 16) & 0xFF);

    seq_number = (seq_number + 1) % 256;

    x25crc_t crc;
    crc_init_custom(&crc);
    crc_accumulate_buffer_custom(&crc, &header[1], sizeof(header) - 1); // skip STX
    crc_accumulate_buffer_custom(&crc, payload, real_length);
    crc_accumulate_custom(&crc, crc_extra);

    size_t packet_len = sizeof(header) + real_length + 2;
    uint8_t *packet = malloc(packet_len);
    if (!packet) {
        return -1;
    }

    memcpy(packet, header, sizeof(header));
    memcpy(packet + sizeof(header), payload, real_length);
    packet[sizeof(header) + real_length] = crc.crc & 0xFF;
    packet[sizeof(header) + real_length + 1] = (crc.crc >> 8) & 0xFF;

    ssize_t sent = sendto(sockfd, packet, packet_len, 0,
                          (const struct sockaddr *)recv_addr, sizeof(*recv_addr));

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
