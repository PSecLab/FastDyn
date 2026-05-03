#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fuzz.h"
#include "afl-fastdyn.h"

#define AFLNET_POLL_TIMEOUT_MS 5000

extern uint8_t CVG[MAP_SIZE];

extern void *afl_main(void *arg);

static pthread_t g_aflnet_thread;
static int g_tap_fd = -1;
static uint64_t g_active_seq = 0;
static uint8_t g_msg_buf[FASTDYN_MAX_FRAME];
static uint8_t g_wire_buf[sizeof(fastdyn_msg_hdr_t) + FASTDYN_MAX_FRAME];

static int dir_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

static bool launch_aflnet(void)
{
    static char *out_dir = "./fastdyn_work/aflnet-out";
    static char *in_dir;
    if (dir_exists(out_dir)) {
        in_dir = "-";
    } else {
        in_dir = "../modbus_seeds/";
    }

    static char *argv[] = {
        "afl-fuzz",
        "-m", "none",
        "-d",
        "-i", "-",
        "-o", "./aflnet-out",
        "-P", "MODBUS",
        "-t", "100000",
        "-D", "100000000",
        "-W", "100000",
        "-h", "1",
        "-K",
        "-R",
        "-X",
        NULL
    };

    for (int i = 0; argv[i]; i++) {
        if (strcmp(argv[i], "-i") == 0 && argv[i + 1]) {
            argv[i + 1] = in_dir;
        }

        if (strcmp(argv[i], "-o") == 0 && argv[i + 1]) {
            argv[i + 1] = out_dir;
        }
    }

    if (pthread_create(&g_aflnet_thread, NULL, afl_main, (void *)argv) != 0) {
        perror("Failed to create AFLNet thread");
        return false;
    }

    return true;
}

static int publish_fd_via_shm(const char *name, int fd)
{
    shm_unlink(name);

    int sfd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (sfd < 0) {
        return -1;
    }

    if (ftruncate(sfd, sizeof(int)) != 0) {
        close(sfd);
        return -1;
    }

    int *p = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
    if (p == MAP_FAILED) {
        close(sfd);
        return -1;
    }

    *p = fd;
    munmap(p, sizeof(int));
    close(sfd);
    return 0;
}

static bool send_fastdyn_msg(fastdyn_msg_type_t type,
                             uint64_t seq,
                             const uint8_t *data,
                             size_t len)
{
    if (g_tap_fd < 0 || len > FASTDYN_MAX_FRAME) {
        return false;
    }

    size_t total = sizeof(fastdyn_msg_hdr_t) + len;
    uint8_t *packet = malloc(total);
    if (packet == NULL) {
        return false;
    }

    fastdyn_msg_hdr_t hdr = {
        .magic = FASTDYN_MSG_MAGIC,
        .type = (uint32_t)type,
        .seq = seq,
        .len = (uint32_t)len,
    };

    memcpy(packet, &hdr, sizeof(hdr));
    if (len != 0) {
        memcpy(packet + sizeof(hdr), data, len);
    }

    ssize_t written = write(g_tap_fd, packet, total);
    free(packet);

    return written == (ssize_t)total;
}

static bool recv_fastdyn_msg(fastdyn_msg_hdr_t *hdr,
                             uint8_t **payload)
{
    ssize_t rd = read(g_tap_fd, g_wire_buf, sizeof(g_wire_buf));
    if (rd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return false;
        }
        perror("[aflnet] failed to read input");
        return false;
    }

    if (rd < (ssize_t)sizeof(*hdr)) {
        return false;
    }

    memcpy(hdr, g_wire_buf, sizeof(*hdr));
    if (hdr->magic != FASTDYN_MSG_MAGIC ||
        hdr->len > FASTDYN_MAX_FRAME ||
        rd != (ssize_t)(sizeof(*hdr) + hdr->len)) {
        return false;
    }

    *payload = g_wire_buf + sizeof(*hdr);
    return true;
}

static bool setup_aflnet_backend(void)
{
    memset(CVG, 0, sizeof(CVG));

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) < 0) {
        perror("[aflnet] socketpair() failed");
        return false;
    }

    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);

    if (publish_fd_via_shm(FASTDYN_SHM_NAME, fds[1]) < 0) {
        perror("[aflnet] shm publish (data fd) failed");
        close(fds[0]);
        close(fds[1]);
        return false;
    }

    g_tap_fd = fds[0];
    g_active_seq = 0;
    return true;
}

static void complete_previous_message(void)
{
    if (g_active_seq == 0) {
        return;
    }

    (void)send_fastdyn_msg(FASTDYN_MSG_DONE, g_active_seq, NULL, 0);
    g_active_seq = 0;
}

static bool read_next_frame(fuzz_backend_msg_t *msg)
{
    while (true) {
        struct pollfd pfd = {
            .fd = g_tap_fd,
            .events = POLLIN,
        };

        int ret = poll(&pfd, 1, AFLNET_POLL_TIMEOUT_MS);

        if (ret == 0) {
            continue;
        }

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("[aflnet] failed to poll for input");
            return false;
        }

        fastdyn_msg_hdr_t hdr;
        uint8_t *payload = NULL;
        if (!recv_fastdyn_msg(&hdr, &payload)) {
            continue;
        }

        if (hdr.type == FASTDYN_MSG_RESTORE) {
            msg->data = NULL;
            msg->len = 0;
            msg->restore = true;
            return true;
        }

        if (hdr.type != FASTDYN_MSG_INPUT) {
            continue;
        }

        memcpy(g_msg_buf, payload, hdr.len);
        g_active_seq = hdr.seq;
        msg->data = g_msg_buf;
        msg->len = hdr.len;
        msg->restore = false;

        return true;
    }
}

bool fuzz_backend_init(void)
{
    if (!setup_aflnet_backend()) {
        return false;
    }

    return launch_aflnet();
}

bool fuzz_backend_next(fuzz_backend_msg_t *msg)
{
    if (msg == NULL || g_tap_fd < 0) {
        return false;
    }

    complete_previous_message();
    return read_next_frame(msg);
}

void fuzz_backend_report_assert(bool fatal)
{
    if (fatal) {
        complete_previous_message();
    }
}

void fuzz_backend_restore_complete(void)
{
    (void)send_fastdyn_msg(FASTDYN_MSG_RESTORE_DONE, 0, NULL, 0);
}

void fuzz_backend_set_data(const uint8_t *buf, size_t len)
{
    if (g_tap_fd < 0 || buf == NULL || len == 0) {
        return;
    }

    if (!send_fastdyn_msg(FASTDYN_MSG_RESPONSE, g_active_seq, buf, len) &&
        errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("[aflnet] failed to write response");
    }
}
