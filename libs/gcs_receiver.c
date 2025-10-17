#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include "../include/ring_buffer.h"
#include "../include/mavlink_lib.h"
#include "mavlink.h"

#define RING_BUFFER_SIZE 512
#define UDP_PORT 14552
#define MAX_PACKET_SIZE 1024

static pthread_t gcs_listener_tid;
static bool gcs_listener_running = false;

static struct sockaddr_in gcs_addr;
// static socklen_t gcs_addr_len = sizeof(gcs_addr);
static int send_sockfd = -1;
static bool send_sockfd_initialized = false;

void sigint_handler(int sig) {
    printf("\n[Main] Caught SIGINT (Ctrl-C). Killing GCS listener thread...\n");
    // Send cancellation request to GCS listener thread
    pthread_cancel(gcs_listener_tid);
    pthread_join(gcs_listener_tid, NULL);
    gcs_listener_running = false;
    printf("[Main] GCS listener thread terminated. Exiting.\n");
    exit(0);
}

// const char * mavlink_dictionary(uint32_t message_id){
//     switch(message_id){
//         case COMMAND_LONG:
//             return "COMMAND_LONG";
//         default:
//             return "UNKNOWN_MESSAGE_ID";
//     }
// }

void translate_and_forward(const mavlink_message_t* msg) {
    // Example: Print message ID and system/component IDs
    if (msg->compid == 220) {
        // Ignore messages from companion computer
        return;
    }

    // translate IDs to human readable message types
    if (msg->msgid == MAVLINK_MSG_ID_COMMAND_LONG) {
        mavlink_command_long_t command;
        mavlink_msg_command_long_decode(msg, &command);
        printf("MAVLink Msg ID: %u (%s) from SysID: %u CompID: %u - Command: %u\n",
            msg->msgid, "COMMAND_LONG", msg->sysid, msg->compid, command.command);
    }
}

// Parser state (can be global or per-connection)
static mavlink_status_t status;
static mavlink_message_t msg;

// Feed one byte at a time
void mavlink_input_byte(uint8_t c) {
    if (mavlink_parse_char(MAVLINK_COMM_0, c, &msg, &status)) {
        // Full message received
        translate_and_forward(&msg);
    }
}

// UDP listener process
static void* gcs_listener(void *rb) {
    RingBuffer * ring_buffer = (RingBuffer *)rb;

    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    unsigned char buffer[MAX_PACKET_SIZE];

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(UDP_PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("[GCSReceiver]: Listening for incoming data on UDP port %d...\n", UDP_PORT);

    // Initialize send socket and GCS address on first packet received
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("send socket creation failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    memset(&gcs_addr, 0, sizeof(gcs_addr));
    gcs_addr.sin_family = AF_INET;
    gcs_addr.sin_port = htons(14551); // GCS port
    gcs_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Assuming GCS is on localhost
    send_sockfd_initialized = true;

    printf("[GCSReceiver]: Initialized send socket to GCS at %s:14551\n",
            inet_ntoa(gcs_addr.sin_addr));

    while (1) {
        socklen_t len = sizeof(cliaddr);
        ssize_t n = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0,
                             (struct sockaddr *)&cliaddr, &len);
        // printf("GCSReceiver: Received %zd bytes\n", n);
        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }

        if (n == 0) {
            // No data received
            sleep(1);
            continue;
        }

        // Put received bytes into the ring buffer
        size_t count = ring_buffer_count(ring_buffer);
        if (count + n > RING_BUFFER_SIZE)
        {
            // Buffer overflow, drop incoming packet
            // printf("GCSReceiver: Buffer overflow, dropping packet\n");
        }
        else
        {
            for (ssize_t i = 0; i < n; i++) {
                if (!ring_buffer_put(ring_buffer, buffer[i])) {
                    printf("GCSReceiver: Failed to put byte into buffer\n");
                    continue;
                }
                // Also feed byte to MAVLink parser
                mavlink_input_byte(buffer[i]);
            }
        }
    }

    close(sockfd);
}

void start_gcs_listener(RingBuffer *rb) {
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Install SIGINT handler (Ctrl-C)
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    pthread_create(&gcs_listener_tid, NULL, gcs_listener, (void *)rb);

    gcs_listener_running = true;
}

// Example function to read a byte
void read_byte(RingBuffer *rb, unsigned char *byte) {
    if (!gcs_listener_running)
    {
        start_gcs_listener(rb);
        usleep(1000);
    }
    if (!ring_buffer_get(rb, byte)) {
        *byte = 0; // Return null byte if empty
    }
    return;
}

// Example function to check bytes available
size_t bytes_available(RingBuffer *rb) {
    if (!gcs_listener_running)
    {
        start_gcs_listener(rb);
        usleep(1000);
    }
    return ring_buffer_count(rb);
}

int send_mavlink_payload(uint32_t message_id,
                         const uint8_t *payload,
                         uint8_t length,
                         uint8_t crc_extra,
                         uint8_t *sequence) {
    if (!send_sockfd_initialized) {
        fprintf(stderr, "Send socket not initialized. Cannot send MAVLink message.\n");
        return -1;
    }
    return mav_finalize_message_chan_send(send_sockfd, &gcs_addr, message_id, payload, length, crc_extra, sequence);

}

void send_mavlink_gps_input(uint8_t system_id, uint8_t component_id, const gps_input_t *gps) {
    if (!gps || send_sockfd < 0) {
        return;
    }

    mavlink_message_t msg;

    // Convert latitude and longitude to int32 (degrees * 1E7)
    int32_t lat = (int32_t)llround(gps->latitude_deg * 1e7);
    int32_t lon = (int32_t)llround(gps->longitude_deg * 1e7);

    // GPS time-of-week in ms
    uint32_t gps_tow_ms = gps->timestamp_sec * 1000 + gps->timestamp_nsec / 1000000;
    uint64_t time_usec = (uint64_t)gps_tow_ms * 1000ULL;

    // Hardcoded GPS week (replace with real computation if desired)
    uint16_t gps_week = 15;

    // Pack the MAVLink GPS_INPUT message (21 arguments)
    mavlink_msg_gps_input_pack(
        system_id,
        component_id,
        &msg,
        time_usec,                        // time_usec
        0,                                // gps_id
        0,                                // ignore_flags
        gps_tow_ms,                        // time_week_ms
        gps_week,                           // time_week
        gps->fix_type,                     // fix_type
        lat,                               // lat
        lon,                               // lon
        (float)(gps->altitude_m * 1000.0f), // alt in mm
        0.0f,                              // hdop
        0.0f,                              // vdop
        gps->velocity_n,                   // vn
        gps->velocity_e,                   // ve
        gps->velocity_d,                   // vd
        0.0f,                              // speed_accuracy
        0.0f,                              // horiz_accuracy
        0.0f,                              // vert_accuracy
        gps->satellites_visible,           // satellites_visible
        36000                              // yaw (in cdeg, 36000 = 360.00 degrees) TODO: make accurate
    );

    // Serialize the message into a buffer
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    uint16_t len = mavlink_msg_to_send_buffer(buffer, &msg);

    // Send over the global UDP socket
    ssize_t sent = sendto(send_sockfd, buffer, len, 0, (struct sockaddr*)&gcs_addr, sizeof(gcs_addr));
    if (sent < 0) {
        perror("sendto");
    }
}


// int main() {
//     RingBuffer rb;
//     if (!ring_buffer_init(&rb, RING_BUFFER_SIZE)) {
//         fprintf(stderr, "Failed to initialize ring buffer\n");
//         return EXIT_FAILURE;
//     }

//     start_gcs_listener(&rb);

//     while (1) {
//         unsigned char b;
//         read_byte(&rb, &b);
//         if (b != 0x00) {
//             printf("Received byte: 0x%02x\n", b);
//         } else {
//             // No data, sleep a bit
//             usleep(1000);
//         }
//     }

//     pthread_join(gcs_listener_tid, NULL);

//     return 0;
// }