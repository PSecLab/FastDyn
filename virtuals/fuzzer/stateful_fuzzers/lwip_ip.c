#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "core.h"
#include "common.h"
#include "fuzz.h"

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

static uint8_t *trace_buffer = NULL;
static size_t trace_size = 0;
static const bool ip_log = false;

#define TRACE_DIR "fastdyn_work/ip_trace/"
#define ETH_MAX_FRAME 1600
#define LWIP_BUF_BASE 0x20020000u
#define LWIP_BUF_COUNT 128

#define LWIP_BUF_MAX (LWIP_BUF_BASE + (sizeof(struct pbuf) + ETH_MAX_FRAME) * LWIP_BUF_COUNT)

static int prev_index = 0; // cycle through available space so that chance of reusing a live buffer is almost 0

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

void fuzz_snap_handler(unsigned int cpu_index, void *udata)
{
    (void)cpu_index;
    (void)udata;
}

void fuzz_eth_in(unsigned int cpu_index, void *udata)
{
    static uint8_t fuzz_eth_buf[ETH_MAX_FRAME];
    struct pbuf buf = {0};

    (void)cpu_index;
    (void)udata;

    // logging for seeds
    if (ip_log) {
        static bool bdir = false;
        if (!bdir) {
            mkdir(TRACE_DIR, 0777);
            bdir = true;
        }
        uint32_t r5 = fuzz_get_register(5);
        if (r5) {
            fuzz_read_memory(r5, (uint8_t *)&buf, sizeof(buf));
            void *payload = malloc(buf.len);
            if (payload == NULL) {
                perror("Couldnt capture trace\n");
            } else {
                fuzz_read_memory(buf.payload, payload, buf.len);
                fuzz_plugin_lwip_ip_dump_trace(payload, buf.len);
                free(payload);
            }
        }
        return;
    }

    size_t rd = fuzz_get_data((char *)fuzz_eth_buf, sizeof(fuzz_eth_buf));
    if (rd == 0) {
        fuzz_set_register(0, 5);
        return;
    }

    if (rd > UINT16_MAX) {
        rd = UINT16_MAX;
    }

    // last four fields are based off of observations
    uint32_t base = LWIP_BUF_BASE + (sizeof(buf) + ETH_MAX_FRAME) * prev_index;
    prev_index++;
    prev_index = prev_index % LWIP_BUF_COUNT;

    buf.next = 0;
    buf.payload = base + sizeof(buf);
    buf.tot_len = (uint16_t)rd;
    buf.len = (uint16_t)rd;
    buf.type_internal = 65;
    buf.flags = 2;
    buf.ref = 1;
    buf.if_idx = 0;

    if (fuzz_write_memory(base, (uint8_t *)&buf, sizeof(buf)) != 0 ||
        fuzz_write_memory(buf.payload, fuzz_eth_buf, (int)rd) != 0) {
        printf("Failed to write in fuzzed memory\n");
    } else {
        fuzz_set_register(base, 5);
    }
}

void fuzz_eth_out(unsigned int cpu_index, void *udata)
{
    struct pbuf buf;

    (void)cpu_index;
    (void)udata;

    if (ip_log) {
        return;
    }

    uint32_t r1 = fuzz_get_register(1);
    if (r1 == 0) {
        printf("IDK, this shouldnt be null, just passed check in binary\n");
        return;
    }

    fuzz_read_memory(r1, (uint8_t *)&buf, sizeof(buf));

    uint8_t *packet = malloc(buf.len);
    if (packet == NULL) {
        printf("Couldnt malloc memory in fuzzer\n");
        return;
    }

    fuzz_read_memory(buf.payload, packet, buf.len);
    fuzz_set_data((char *)packet, buf.len);
    free(packet);
}

void fuzz_pbuf_free(unsigned int cpu_index, void *udata)
{
    (void)cpu_index;
    (void)udata;

    uint32_t r0 = fuzz_get_register(0);
    if (r0 >= LWIP_BUF_BASE && r0 < LWIP_BUF_MAX) {
        fuzz_set_register(0, 0);
    }
}