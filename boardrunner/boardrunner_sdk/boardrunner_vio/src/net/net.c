#include <boardrunner/net.h>

// --- NETWORK BACKEND HELPERS ---

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>

/**
 * api_tap_init:
 * Opens the /dev/net/tun device and configures it as a TAP interface.
 * Returns the file descriptor (fd) on success, or -1 on error.
 */
int api_tap_init(const char *dev_name) {
    struct ifreq ifr;
    int fd, err;

    // FIX: Add O_NONBLOCK here so read() returns immediately if empty
    if ((fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK)) < 0) {
        perror("ERR: Could not open /dev/net/tun");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    if (*dev_name) {
        strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);
    }

    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
        perror("ERR: ioctl(TUNSETIFF) failed");
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * api_tap_send:
 * Takes a raw Ethernet frame (buffer) and writes it to the TAP fd.
 * This effectively "receives" the packet on the Host Linux side.
 */
int api_tap_send(int fd, const uint8_t *buf, int len) {
    return write(fd, buf, len);
}

/**
 * api_tap_recv_nonblock:
 * Polls the TAP fd for incoming packets from the Host.
 * Returns the number of bytes read, or -1 if no packet is waiting (EAGAIN).
 */
int api_tap_recv_nonblock(int fd, uint8_t *buf, int max_len) {
    // Standard non-blocking read
    // Note: The file descriptor should be set to O_NONBLOCK mode
    // or this function should be used with select()/poll()
    return read(fd, buf, max_len);
}