#include <boardrunner/fileio.h>

int api_file_open(const char *filename, int mode) {
    int flags = (mode == 0) ? O_RDONLY : (O_RDWR | O_CREAT);
    int fd = open(filename, flags, 0644);
    if (fd < 0) perror("API_FILE: open failed");
    return fd;
}

int api_file_pread(int fd, void *buf, int len, uint64_t offset) {
    if (fd < 0 || !buf || len < 0) return -1;

    uint8_t *p = (uint8_t *)buf;
    int total = 0;

    while (total < len) {
        ssize_t n = pread(fd, p + total, (size_t)(len - total), (off_t)(offset + (uint64_t)total));
        if (n == 0) {
            // EOF: zero-fill remainder so callers always get deterministic data
            memset(p + total, 0, (size_t)(len - total));
            return len;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("API_FILE: pread failed");
            // zero-fill to avoid leaking uninitialized data upward
            memset(p + total, 0, (size_t)(len - total));
            return -1;
        }
        total += (int)n;
    }
    return len;
}

int api_file_pwrite(int fd, const void *buf, int len, uint64_t offset) {
    if (fd < 0 || !buf || len < 0) return -1;

    const uint8_t *p = (const uint8_t *)buf;
    int total = 0;

    while (total < len) {
        ssize_t n = pwrite(fd, p + total, (size_t)(len - total), (off_t)(offset + (uint64_t)total));
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("API_FILE: pwrite failed");
            return -1;
        }
        total += (int)n;
    }
    return len;
}

uint64_t api_file_get_size(int fd) {
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("API_FILE: fstat failed");
        return 0;
    }
    return (uint64_t)st.st_size;
}

void api_file_close(int fd) {
    if (fd >= 0) close(fd);
}