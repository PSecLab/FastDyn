#include <dlfcn.h>
#include <utils.h>
#include <core.h>
#include <common.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <immintrin.h>
#include <pthread.h>
#include <poll.h>

#include "fuzz.h"
#include "afl-fastdyn.h"

#include "core.h"
#include "common.h"
#include "fuzz_trace.h"

// since we are using aflnet, we need to define CVG
uint8_t CVG[MAP_SIZE];

#define LWIP_PBUF_REF_T uint8_t

// first two are pointers, but since the target is 32 bit were just doing this to properly store & write values

struct pbuf {
  /** next pbuf in singly linked pbuf chain */
  uint32_t next;

  /** pointer to the actual data in the buffer */
  uint32_t payload;

  /**
   * total length of this buffer and all next buffers in chain
   * belonging to the same packet.
   *
   * For non-queue packet chains this is the invariant:
   * p->tot_len == p->len + (p->next? p->next->tot_len: 0)
   */
  uint16_t tot_len;

  /** length of this buffer */
  uint16_t len;

  /** a bit field indicating pbuf type and allocation sources
      (see PBUF_TYPE_FLAG_*, PBUF_ALLOC_FLAG_* and PBUF_TYPE_ALLOC_SRC_MASK)
    */
  uint8_t type_internal;

  /** misc flags */
  uint8_t flags;

  /**
   * the reference count always equals the number of pointers
   * that refer to this pbuf. This can be pointers from an application,
   * the stack itself, or pbuf->next pointers from a chain.
   */
  LWIP_PBUF_REF_T ref;

  /** For incoming packets, this contains the input netifs index */
  uint8_t if_idx;
};

static pthread_t taflnet;
static uint8_t *trace_buffer = NULL;
static size_t trace_size = 0;
static int tap_fd = -1;
static int loop_evt_fd = -1;  // signals recv is either done or packet dropped
static fastdyn_sync_state_t *sync_state = NULL;
static uint64_t last_acked_tx_seq = 0;
static const bool ip_log = false;

#define TRACE_DIR "fastdyn_work/ip_trace/"

static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

static int fuzz_plugin_lwip_ip_dump_trace(uint8_t *buf, size_t size)
{
    /* Derive the true frame length from the Ethernet header rather than
     * trusting pbuf.len directly, which may include trailing pbuf padding.
     * size (pbuf.len) is used as the hard upper bound. */
    size_t frame_len = size;

    if (size >= 14) {
        uint16_t ethertype = ((uint16_t)buf[12] << 8) | buf[13];

        if (ethertype == 0x0800 && size >= 14 + 4) {
            /* IPv4: true length from IP total-length field */
            uint16_t ip_total = ((uint16_t)buf[14 + 2] << 8) | buf[14 + 3];
            size_t computed = 14 + ip_total;
            if (computed <= size) frame_len = computed;
        } else if (ethertype == 0x0806) {
            /* ARP over Ethernet/IPv4 is always 42 bytes */
            if (size >= 42) frame_len = 42;
        }
    }

    trace_buffer = realloc(trace_buffer, trace_size + frame_len);
    if (trace_buffer == NULL) {
        perror("Failed to allocate for trace\n");
        return -1;
    }

    memcpy(trace_buffer + trace_size, buf, frame_len);

    trace_size += frame_len;

    return 0;
}

extern void *afl_main(void* arg);

static void launch_aflnet() {
    static char* out_dir = "./fastdyn_work/http-out";
    static char* in_dir;
    if (dir_exists(out_dir)) {
        in_dir = "-";
    } else {
        in_dir = "../eth_seeds/";
    }
    
    static char* argv[] = {
        "afl-fuzz",
        "-m", "none",
        "-d",
        "-i", "-",
        "-o", "./http-out",
        "-P", "ETHERNET",
        "-t", "20000",
        "-D", "100000000",
        "-W", "20000",
        "-q", "3",
        "-s", "3",
        "-E",
        "-K",
        "-R",
        "-X",
        NULL
    };
    for (int i = 0; argv[i]; i++) {
        if (strcmp(argv[i], "-i") == 0 && argv[i+1])
            argv[i+1] = in_dir;

        if (strcmp(argv[i], "-o") == 0 && argv[i+1])
            argv[i+1] = out_dir; // probably the same, but just want to make it easy to configure in future
    }

    if (pthread_create(&taflnet, NULL, afl_main, (void*)argv) != 0) {
        perror("Failed to create thread\n");
    }
}

#define FASTDYN_SHM_NAME      "/fastdyn_fuzzer_fd"
#define FASTDYN_LOOP_SHM_NAME "/fastdyn_loop_fd"

static int publish_fd_via_shm(const char *name, int fd) {
    shm_unlink(name);
    int sfd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (sfd < 0) return -1;
    ftruncate(sfd, sizeof(int));
    int *p = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
    if (p == MAP_FAILED) { close(sfd); return -1; }
    *p = fd;
    munmap(p, sizeof(int));
    close(sfd);
    return 0;
}

static int setup_sync_state_shm(void) {
    shm_unlink(FASTDYN_SYNC_SHM_NAME);

    int sfd = shm_open(FASTDYN_SYNC_SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (sfd < 0) return -1;

    if (ftruncate(sfd, sizeof(*sync_state)) != 0) {
        close(sfd);
        return -1;
    }

    sync_state = mmap(NULL, sizeof(*sync_state), PROT_READ | PROT_WRITE,
                      MAP_SHARED, sfd, 0);
    close(sfd);

    if (sync_state == MAP_FAILED) {
        sync_state = NULL;
        return -1;
    }

    atomic_store_explicit(&sync_state->tx_seq, 0, memory_order_relaxed);
    atomic_store_explicit(&sync_state->ack_seq, 0, memory_order_relaxed);
    last_acked_tx_seq = 0;
    return 0;
}

int  g_snap_done_fd    = -1;  /* eventfd: written after restore, read by fastdyn_snap_restore() */
static int fuzz_eth_setup() {
    mkdir(TRACE_DIR, 0777);
    memset(CVG, 0, sizeof(CVG));

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) < 0) {
      perror("[eth] socketpair() failed");
      return -1;
    }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);

    if (publish_fd_via_shm(FASTDYN_SHM_NAME, fds[1]) < 0) {
      perror("[eth] shm publish (data fd) failed");
      return -1;
    }

    /* EFD_NONBLOCK so the drain in fastdyn_send never blocks. */
    loop_evt_fd = eventfd(0, EFD_NONBLOCK);
    if (loop_evt_fd < 0) {
      perror("[eth] eventfd() failed");
      return -1;
    }
    if (publish_fd_via_shm(FASTDYN_LOOP_SHM_NAME, loop_evt_fd) < 0) {
      perror("[eth] shm publish (loop fd) failed");
      return -1;
    }

    if (setup_sync_state_shm() < 0) {
      perror("[eth] shm publish (sync state) failed");
      return -1;
    }

    tap_fd = fds[0];

    /* Blocking eventfd (no EFD_NONBLOCK): written once per restore by
     * fuzz_snap_handler, consumed once per call by fastdyn_snap_restore. */
    g_snap_done_fd = eventfd(0, 0);
    if (g_snap_done_fd < 0) {
        perror("[eth] eventfd() for snap_done failed");
        return -1;
    }

    return 0;
}

static void drain_fd(int fd) {
    uint8_t buf[2048];

    while (1) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // queue empty
            }
            break; // error or closed
        }
    }
}

bool fuzz_snapshot_flag = false;
// void fuzz_snap_handler(unsigned int cpu_index, void *udata) { // used as virtual
void fuzz_snap_handler() { // called from virtual
    static bool snap_taken = false;
    static uint32_t regs[16];

    if (ip_log) return;

    if (!snap_taken) {
        if (fuzz_snap_memory() != 0) {
            perror("Failed to take initial snapshot\n");
            return;
        }
        for (int i = 0; i < 15; i++) {
            regs[i] = fuzz_get_register(i);
        }
        //printf("Saved pc = %x\n", regs[15]);
        snap_taken = true;
    } else if (fuzz_snapshot_flag) {
        if (tap_fd != -1) {
            drain_fd(tap_fd);
        }
        if (fuzz_restore_memory() != 0) {
            perror("Failed to restore snapshot\n");
            fuzz_snapshot_flag = false;
            if (g_snap_done_fd >= 0) {
                static const uint64_t one = 1;
                write(g_snap_done_fd, &one, sizeof(one));
            }
            return;
        }
        for (int i = 0; i < 15; i++) {
            fuzz_set_register(regs[i], i);
        }

        // Commit the trace and compare against the baseline before waking aflnet.
        // fuzz_snapshot_flag acts as the release barrier: aflnet spin-waits
        // on it, so everything written here is visible when it returns.
        fuzz_trace_commit_run();
        fuzz_trace_compare();

        fuzz_snapshot_flag = false;

        /* Unblock fastdyn_snap_restore().  Written after the restore is fully
         * complete so the aflnet thread never sees a partially-restored state. */
        if (g_snap_done_fd >= 0) {
            static const uint64_t one = 1;
            write(g_snap_done_fd, &one, sizeof(one));
        }
    }
}

#define ETH_MAX_FRAME 1600
void fuzz_eth_in(unsigned int cpu_index, void *udata) {
    static bool fuzz_eth_init = false;
    static uint8_t fuzz_eth_buf[ETH_MAX_FRAME];

    struct pbuf buf;

    // logging for seeds
    if (ip_log) {
        static bool bdir = false;
        if (!bdir) {
            mkdir(TRACE_DIR, 0777);
            bdir = true;
        }
        uint32_t r5 = fuzz_get_register(5);
        if (r5) {
            fuzz_read_memory(r5, &buf, sizeof(buf));
            void *payload = malloc(buf.len);
            if (payload == NULL) {
                perror("Couldnt capture trace\n");
            } else {
                fuzz_read_memory(buf.payload, payload, buf.len);
                fuzz_plugin_lwip_ip_dump_trace(payload, buf.len);
            }
        }
        return;
    }

    const uint32_t caddr = 0x2000D000;

    if (!fuzz_eth_init) {
        if (fuzz_eth_setup() != 0) {
            perror("Failed to set up ethernet handle\n");
            return;
        }

        launch_aflnet();
        fuzz_eth_init = true;
    }

    if (tap_fd == -1) return;

check_snap:
    fuzz_snap_handler();

    /* Acknowledge exactly one newly-submitted packet per return to the input
     * loop. The eventfd is just a wake-up hint; tx_seq/ack_seq disambiguate
     * stale wakes from the completion of the current packet. */
    bool did_ack = false;
    if (sync_state) {
        uint64_t tx_seq = atomic_load_explicit(&sync_state->tx_seq,
                                               memory_order_acquire);
        if (tx_seq != 0 && tx_seq != last_acked_tx_seq) {
            atomic_store_explicit(&sync_state->ack_seq, tx_seq,
                                  memory_order_release);
            last_acked_tx_seq = tx_seq;
            did_ack = true;
        }
    }

    if (loop_evt_fd >= 0 && did_ack) {
        static const uint64_t one = 1;
        write(loop_evt_fd, &one, sizeof(one));
    }

    int rd = 0;
    struct pollfd pfd = {
        .fd = tap_fd,
        .events = POLLIN
    };

    int ret = poll(&pfd, 1, 5000); // timeout in ms

    if (ret > 0) { // message ready
        rd = read(tap_fd, fuzz_eth_buf, sizeof(fuzz_eth_buf));
        if (rd == 4 && *(uint32_t*)fuzz_eth_buf == 0x13243546) {
            fuzz_snapshot_flag = true;
            goto check_snap;
        }
    } else if (ret == 0) { // timeout
        printf("aflnet took too long to produce input (way longer than should ever happen)\n");
    } else {
        printf("failed to poll for input\n");
    }
    if (rd <= 0) {
        fuzz_set_register(0, 5);
        return;
    }
    fuzz_trace_on_inject();

    // last four fields are based off of observations
    buf.next = NULL;
    buf.payload = (void*)(caddr + sizeof(buf));
    buf.tot_len = rd; // equal to len with no next
    buf.len = rd;
    buf.type_internal = 65;
    buf.flags = 2;
    buf.ref = 1;
    buf.if_idx = 0;

    if (fuzz_write_memory(caddr, &buf, sizeof(buf)) != 0 || fuzz_write_memory(buf.payload, fuzz_eth_buf, rd) != 0) {
        printf("Failed to write in fuzzed memory\n");
    } else {
        fuzz_set_register(caddr, 5);
    }

    return;
}

void fuzz_eth_out(unsigned int cpu_index, void *udata) {
    struct pbuf buf;

    if (tap_fd == -1 || ip_log) return;

    uint32_t r1 = fuzz_get_register(1);
    if (r1 == 0) {
        printf("IDK, this shouldnt be null, just passed check in binary\n");
        return;
    }

    fuzz_read_memory(r1, &buf, sizeof(buf));

    uint8_t *packet = malloc(buf.len);
    if (packet == NULL) {
        printf("Couldnt malloc memory in fuzzer\n");
        return;
    }

    fuzz_read_memory(buf.payload, packet, buf.len);

    write(tap_fd, packet, buf.len);
    free(packet);
}

void fuzz_pbuf_free(unsigned int cpu_index, void *udata) {
    uint32_t r0 = fuzz_get_register(0);
    if (r0 == 0x2000D000) {
        fuzz_set_register(0, 0);
    }
}

void fuzz_plugin_lwip_ip_fuzzer_exit(void) {
    if (ip_log) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s/trace.raw", TRACE_DIR);

        FILE *f = fopen(tmp, "wb");
        if (!f) {
            perror("fopen");
            return;
        }

        size_t written = fwrite(trace_buffer, 1, trace_size, f);
        if (written != trace_size) {
            perror("fwrite");
        }
        fclose(f);
    }
}