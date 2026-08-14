#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <virtuals.h>
#include <utils.h>

#include "cov_trace.h"
#include "fuzz.h"
#include "core.h"
#include "mavlink.h"

#define MAX_SIZE_PROBABLY 1024 // doesn't need to be exact, just what do we want our max size to be, much larger than an actual mavlink_message_t

#define TRACE_ITERS 1000 // will pick the thousandth fuzzed message to copy and trace execution.
static const bool mvlnk_trace = false;
static int mvlnk_iters = 0;

static uint8_t buffer[MAX_SIZE_PROBABLY];
static size_t len = 0;
static size_t ptr = 0;

// static uint8_t fuzz_mvlnk_next_input_byte(size_t *input_ptr)
// {
//     if (*input_ptr >= len) {
//         return 0;
//     }

//     return buffer[(*input_ptr)++];
// }

// static void fuzz_mvlnk_encode_data(void)
// {
//     mavlink_message_t msg;
//     const mavlink_msg_entry_t *entry;
//     uint8_t wire[MAVLINK_MAX_PACKET_LEN];
//     uint8_t *payload;
//     size_t input_ptr = ptr;
//     uint8_t length_selector;
//     uint8_t min_msg_len;
//     uint8_t max_msg_len;
//     uint8_t crc_extra;
//     size_t payload_input_len;
//     size_t payload_copy_len;
//     size_t wire_len = 0;
//     bool mavlink1;

//     if (input_ptr >= len) {
//         //return;
//     }

//     memset(&msg, 0, sizeof(msg));

//     /*
//      * The first byte after the timestamp words controls whether len is derived
//      * or fuzzed. Every other frame field comes directly from fuzz input.
//      */
//     length_selector = fuzz_mvlnk_next_input_byte(&input_ptr);
//     msg.magic = MAVLINK_STX_MAVLINK1;
//     msg.incompat_flags = fuzz_mvlnk_next_input_byte(&input_ptr);
//     msg.compat_flags = fuzz_mvlnk_next_input_byte(&input_ptr);
//     msg.seq = fuzz_mvlnk_next_input_byte(&input_ptr);
//     msg.sysid = fuzz_mvlnk_next_input_byte(&input_ptr);
//     msg.compid = fuzz_mvlnk_next_input_byte(&input_ptr);
//     msg.msgid = fuzz_mvlnk_next_input_byte(&input_ptr);
//     msg.msgid |= ((uint32_t)fuzz_mvlnk_next_input_byte(&input_ptr)) << 8;
//     msg.msgid |= ((uint32_t)fuzz_mvlnk_next_input_byte(&input_ptr)) << 16;
//     mavlink1 = msg.magic == MAVLINK_STX_MAVLINK1;
//     if (mavlink1) {
//         msg.msgid &= 0xff;
//     }

//     entry = mavlink_get_msg_entry(msg.msgid);
//     min_msg_len = entry ? entry->min_msg_len : 0;
//     max_msg_len = entry ? entry->max_msg_len : MAVLINK_MAX_PAYLOAD_LEN;
//     crc_extra = entry ? entry->crc_extra : 0;

//     payload = (uint8_t *)_MAV_PAYLOAD_NON_CONST(&msg);
//     payload_input_len = len - input_ptr;
//     if (length_selector < 128) {
//         payload_copy_len = payload_input_len;
//         if (payload_copy_len > max_msg_len) {
//             payload_copy_len = max_msg_len;
//         }

//         if (mavlink1) {
//             msg.len = min_msg_len;
//             if (payload_copy_len > msg.len) {
//                 payload_copy_len = msg.len;
//             }
//         } else {
//             msg.len = (uint8_t)payload_copy_len;
//             if (msg.len < min_msg_len) {
//                 msg.len = min_msg_len;
//             }
//         }
//     } else {
//         msg.len = length_selector;
//         payload_copy_len = payload_input_len;
//         if (payload_copy_len > msg.len) {
//             payload_copy_len = msg.len;
//         }
//     }
//     memcpy(payload, &buffer[input_ptr], payload_copy_len);
//     input_ptr += payload_copy_len;

//     if (!mavlink1 && (msg.incompat_flags & MAVLINK_IFLAG_SIGNED)) {
//         for (uint8_t i = 0; i < MAVLINK_SIGNATURE_BLOCK_LEN; i++) {
//             msg.signature[i] = fuzz_mvlnk_next_input_byte(&input_ptr);
//         }
//     }

//     /*
//      * Match mavlink_frame_char_buffer(): its checksum starts at len, walks
//      * every remaining header and payload byte, then includes CRC_EXTRA.
//      */
//     mavlink_start_checksum(&msg);
//     mavlink_update_checksum(&msg, msg.len);
//     if (!mavlink1) {
//         mavlink_update_checksum(&msg, msg.incompat_flags);
//         mavlink_update_checksum(&msg, msg.compat_flags);
//     }
//     mavlink_update_checksum(&msg, msg.seq);
//     mavlink_update_checksum(&msg, msg.sysid);
//     mavlink_update_checksum(&msg, msg.compid);
//     mavlink_update_checksum(&msg, msg.msgid & 0xff);
//     if (!mavlink1) {
//         mavlink_update_checksum(&msg, (msg.msgid >> 8) & 0xff);
//         mavlink_update_checksum(&msg, (msg.msgid >> 16) & 0xff);
//     }
//     for (uint8_t i = 0; i < msg.len; i++) {
//         mavlink_update_checksum(&msg, payload[i]);
//     }
//     mavlink_update_checksum(&msg, crc_extra);

//     /*
//      * Do not use mavlink_msg_to_send_buffer() here. It trims trailing
//      * MAVLink 2 zero bytes, while zero padding is required to reach min_msg_len.
//      */
//     wire[wire_len++] = msg.magic;
//     wire[wire_len++] = msg.len;
//     if (!mavlink1) {
//         wire[wire_len++] = msg.incompat_flags;
//         wire[wire_len++] = msg.compat_flags;
//     }
//     wire[wire_len++] = msg.seq;
//     wire[wire_len++] = msg.sysid;
//     wire[wire_len++] = msg.compid;
//     wire[wire_len++] = msg.msgid & 0xff;
//     if (!mavlink1) {
//         wire[wire_len++] = (msg.msgid >> 8) & 0xff;
//         wire[wire_len++] = (msg.msgid >> 16) & 0xff;
//     }
//     memcpy(&wire[wire_len], payload, msg.len);
//     wire_len += msg.len;
//     wire[wire_len++] = msg.checksum & 0xff;
//     wire[wire_len++] = msg.checksum >> 8;
//     if (!mavlink1 && (msg.incompat_flags & MAVLINK_IFLAG_SIGNED)) {
//         memcpy(&wire[wire_len], msg.signature, MAVLINK_SIGNATURE_BLOCK_LEN);
//         wire_len += MAVLINK_SIGNATURE_BLOCK_LEN;
//     }

//     memcpy(&buffer[ptr], wire, wire_len);
//     len = ptr + wire_len;
// }

// // replaces _port->available()
// static void fuzz_mvlnk_produce_data(unsigned int cpu_index, void *udata)
// {
//     // if mvlnk trace is enabled, force tracing on the TRACE_ITERS input
//     if (!mvlnk_trace || mvlnk_iters < TRACE_ITERS) {
//         len = fuzz_get_data((char*)buffer, sizeof(buffer));
//         mvlnk_iters++;
//     }
//     ptr = 0;

//     if (len >= 8) {
//         fuzz_set_register(*(uint32_t*)(buffer + ptr), 6); // millis
//         fuzz_set_register(*(uint32_t*)(buffer + ptr + 4), 9); // micros
//         ptr += 8;
//     }

//     fuzz_mvlnk_encode_data();

//     //printf("producer: %u\n", len);

//     uint32_t r5 = fuzz_get_register(5);
//     if (r5 != 0) {
//         utils_die("r5 is not reset\n");
//     }
//     fuzz_set_register(len - ptr, 0);

//     // if we've reached trace message, prepare for trace
//     static bool cleared_trace = false;
//     if (mvlnk_trace && mvlnk_iters >= TRACE_ITERS) {
//         core_wait_for_trace_drain();
//         fuzz_trace_enable(-1);

//         if (!cleared_trace) {
//             printf("Enabling trace\n");
//             fuzz_trace_reset();
//             cleared_trace = true;
//         }
//     }
// }

// // replaces _port->read() (this is a single byte returned)
// static void fuzz_mvlnk_consume_data(unsigned int cpu_index, void *udata)
// {
//     if (ptr >= len) {
//         printf("[mvlnk] Over-consuming buffer: %lu %lu\n", ptr, len);
//         fuzz_set_register(0, 0);
//         return;
//     }
//     //printf("consumer\n");

//     //printf("hi %d\n", ptr);
//     fuzz_set_register(buffer[ptr++], 0);
// }

// static void fuzz_mvlnk_ned(unsigned int cpu_index, void *udata) {
//     uint8_t buf[sizeof(mavlink_set_position_target_local_ned_t)];
//     memset(buf, 0, sizeof(buf));

//     size_t len = fuzz_get_data((char*)buf, sizeof(buf));

//     printf("hi\n");

//     // int fuzz_write_memory(unsigned long long addr, uint8_t *mem_buf, int len);
//     //fuzz_write_memory(fuzz_get_register(1), buf, len);
// }

static void fuzz_mvlnk_packet_received(unsigned int cpu_index, void *udata)
{
    memset(buffer, 0, 291);
    len = fuzz_get_data((char*)buffer, sizeof(buffer));
    buffer[2] = 0xFD;

    fuzz_write_memory(fuzz_get_register(2), buffer, 291); // overwrite the mavlink message
}

extern bool g_trace_enabled;
bool fuzz_restore_snapshot(void);
void fuzz_mvlnk_callback(void)
{
    // handle tracing if enabled
    if (mvlnk_trace && g_trace_enabled) {
        fuzz_trace_commit_run();
        fuzz_trace_compare();
        fuzz_trace_disable();

        // force a snapshot
        if (!fuzz_restore_snapshot()) {
            utils_die("[fuzz_sync] Failed to restore snapshot");
        }
    }
}

void fuzz_mvlnk_init(void)
{
    // virtual_register("fuzz_mvlnk_produce_data", fuzz_mvlnk_produce_data);
    // virtual_register("fuzz_mvlnk_consume_data", fuzz_mvlnk_consume_data);
    virtual_register("fuzz_mvlnk_packet_received", fuzz_mvlnk_packet_received);
    // virtual_register("fuzz_mvlnk_ned", fuzz_mvlnk_ned);
}
