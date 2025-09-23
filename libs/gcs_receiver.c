#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include "../include/ring_buffer.h"

#define RING_BUFFER_SIZE 512
#define UDP_PORT 14552
#define MAX_PACKET_SIZE 1024

// Signal handler for graceful shutdown
void handle_sigterm(int sig) {
    printf("Received signal %d, exiting...\n", sig);
    exit(0);
}

// UDP listener process
void* gcs_listener(void *rb) {
    RingBuffer * ring_buffer = (RingBuffer *)rb;

    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    unsigned char buffer[MAX_PACKET_SIZE];

    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigterm);

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

    printf("GCSReceiver: Listening for incoming data on UDP port %d...\n", UDP_PORT);

    while (1) {
        socklen_t len = sizeof(cliaddr);
        ssize_t n = recvfrom(sockfd, buffer, MAX_PACKET_SIZE, 0,
                             (struct sockaddr *)&cliaddr, &len);
        printf("GCSReceiver: Received %zd bytes\n", n);
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
        size_t count = ring_buffer_count(rb);
        if (count + n > RING_BUFFER_SIZE)
        {
            // Buffer overflow, drop incoming packet
            printf("GCSReceiver: Buffer overflow, dropping packet\n");
        }
        else
        {
            for (ssize_t i = 0; i < n; i++) {
                if (!ring_buffer_put(rb, buffer[i])) {
                    printf("GCSReceiver: Failed to put byte into buffer\n");
                    // Buffer full, drop byte
                    // printf("GCSReceiver: Buffer full, dropping byte\n");
                    continue;
                }
            }
        }
    }

    close(sockfd);
}

// Example function to read a byte
int read_byte(RingBuffer *rb, unsigned char *byte) {
    if (!ring_buffer_get(rb, byte)) {
        *byte = 0; // Return null byte if empty
        return -1;
    }
    return 0;
}

// Example function to check bytes available
size_t bytes_available(RingBuffer *rb) {
    return ring_buffer_count(rb);
}

int main() {
    RingBuffer rb;
    if (ring_buffer_init(&rb, RING_BUFFER_SIZE) != 0) {
        fprintf(stderr, "Failed to initialize ring buffer\n");
        return EXIT_FAILURE;
    }

    pthread_t tid;
    pthread_create(&tid, NULL, gcs_listener, &rb);

    while (1) {
        unsigned char b;
        if (read_byte(&rb, &b) == 0) {
            printf("Received byte: 0x%02x\n", b);
        } else {
            // No data, sleep a bit
            usleep(1000);
        }
    }

    pthread_join(tid, NULL);

    return 0;
}