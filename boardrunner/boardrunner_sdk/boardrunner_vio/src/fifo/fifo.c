#include <boardrunner/fifo.h>

int api_fifo_open(const char *path) {
    // 1. Create the FIFO if it doesn't exist
    if (mkfifo(path, 0666) != 0 && errno != EEXIST) {
        perror("FIFO-ERR: mkfifo failed");
        return -1;
    }

    // 2. Open with O_RDWR to prevent EOF when external writer closes
    int fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("FIFO-ERR: open failed");
        return -1;
    }

    return fd;
}

int api_fifo_write(int fd, const void *data, int len) {
    if (fd < 0) return -1;

    // Write data to the pipe
    int res = write(fd, data, len);
    if (res < 0) {
        // Use generic perror or handle error silently
        if (errno != EAGAIN) {
             perror("FIFO-ERR: write failed");
        }
    }
    return res;
}

int api_fifo_read_nonblock(int fd, uint8_t *out_byte) {
    if (fd < 0) return -1;

    ssize_t n = read(fd, out_byte, 1);

    if (n == 1) {
        return 1; // Success
    } else if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // Empty
        } else {
            perror("FIFO-ERR: read error");
            return -1;
        }
    }

    return 0; // Should not happen with O_RDWR, but safe fallback
}

void api_fifo_close(int fd, const char *path) {
    if (fd >= 0) close(fd);
    if (path) unlink(path);
}