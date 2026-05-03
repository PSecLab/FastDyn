#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core.h"
#include "common.h"
#include "fuzz.h"

#define TRACE_DIR "fastdyn_work/mb_trace/"

static int prev_index = 0; // cycle through available space so that chance of reusing a live buffer is almost 0
static FILE *g_mb_trace_file = NULL;
static bool g_mb_trace_exit_hook_registered = false;

#define USART2_IRQ_NUM   54   /* NVIC IRQn 38 => exception 54 */

static void fuzz_modbus_close_trace(void)
{
    if (g_mb_trace_file == NULL) {
        return;
    }

    fclose(g_mb_trace_file);
    g_mb_trace_file = NULL;
}

static bool fuzz_modbus_open_trace(void)
{
    char trace_path[256];

    if (g_mb_trace_file != NULL) {
        return true;
    }

    if (mkdir(TRACE_DIR, 0777) != 0 && errno != EEXIST) {
        perror("mkdir mb trace dir");
        return false;
    }

    for (unsigned int trace_index = 0;; trace_index++) {
        int path_len = snprintf(trace_path, sizeof(trace_path),
                                TRACE_DIR "trace%u.raw", trace_index);
        if (path_len < 0 || (size_t)path_len >= sizeof(trace_path)) {
            printf("Modbus trace path too long\n");
            return false;
        }

        if (access(trace_path, F_OK) == 0) {
            continue;
        }

        if (errno != ENOENT) {
            perror("access mb trace file");
            return false;
        }

        g_mb_trace_file = fopen(trace_path, "ab");
        if (g_mb_trace_file == NULL) {
            perror("fopen mb trace file");
            return false;
        }

        if (!g_mb_trace_exit_hook_registered) {
            core_register_exit_hook(fuzz_modbus_close_trace);
            g_mb_trace_exit_hook_registered = true;
        }

        return true;
    }
}

static bool can_receive = true;

void fuzz_modbus_enable() {
    can_receive = true;
}

// r4 holds the status register
void fuzz_modbus_MB_USART_Poll(unsigned int cpu_index, void *udata)
{
    (void)cpu_index;
    (void)udata;

    #define USART_SR_RXNE    (1u << 5)
    #define USART_SR_TXE     (1u << 7)

    //printf("Setting status register\n");
    
    uint8_t sr = USART_SR_TXE;
    if (can_receive || true) {
        sr |= USART_SR_RXNE;
    }

    fuzz_set_register(sr, 4);
}

// r3 gets the input byte, we can overwrite at 0x80008d0
void fuzz_xMBPortSerialGetByte(unsigned int cpu_index, void *udata)
{
    static uint8_t modbus_buffer[512]; // just for fun make it ~2x as big as max modbus size, though it will likely stop receiving past a point
    static size_t buf_pointer = 0;
    static size_t buf_top = 0;

    (void)cpu_index;
    (void)udata;

    uint8_t mb_byte = (uint8_t)fuzz_get_register(3);

    printf(" <- %02x\n", mb_byte);

    return;

    // logging for seeds
    if (false) {
        if (fuzz_modbus_open_trace() &&
            (fwrite(&mb_byte, sizeof(mb_byte), 1, g_mb_trace_file) != 1 ||
             fflush(g_mb_trace_file) != 0)) {
            perror("write mb trace byte");
        }
    }

    if (buf_pointer >= buf_top) {
        printf("Getting more data :)\n");

        size_t rd = fuzz_get_data((char *)modbus_buffer, sizeof(modbus_buffer));
        if (rd == 0) {
            perror("fuzz_get_data failed");
        }

        buf_pointer = 0;
        buf_top = rd;
    }

    mb_byte = modbus_buffer[buf_pointer++]; // consume next byte of packet

    printf(" <- %02x\n", mb_byte);

    fuzz_set_register(mb_byte, 3);

    if (buf_pointer >= buf_top) { // we shouldn't receive any more
        can_receive = false;
    }
}

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint16_t modbus_crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t pos = 0; pos < len; pos++) {
        crc ^= buf[pos];

        for (int i = 0; i < 8; i++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }

    return crc;
}

static int modbus_rtu_crc_ok(const uint8_t *buf, size_t len)
{
    if (len < 4)
        return 0;

    uint16_t calc = modbus_crc16(buf, len - 2);
    uint16_t got = (uint16_t)buf[len - 2] |
                   ((uint16_t)buf[len - 1] << 8);

    return calc == got;
}

/*
 * Return values:
 *   0  = not done yet
 *   1  = complete valid-looking Modbus RTU response
 *  -1  = malformed / impossible length
 */
static int modbus_rtu_response_done(const uint8_t *buf,
                                    size_t len,
                                    size_t *expected_len_out)
{
    size_t expected = 0;

    if (expected_len_out)
        *expected_len_out = 0;

    if (len < 2)
        return 0;

    uint8_t func = buf[1];

    if (func & 0x80) {
        expected = 5;  // [addr][func|0x80][exception][crc_lo][crc_hi]
    } else {
        switch (func) {
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
            if (len < 3)
                return 0;

            expected = 3 + buf[2] + 2;
            break;

        case 0x05:
        case 0x06:
        case 0x0F:
        case 0x10:
            expected = 8;
            break;

        case 0x07:
            expected = 5;
            break;

        case 0x11:
            if (len < 3)
                return 0;

            expected = 3 + buf[2] + 2;
            break;

        default:
            return -1;
        }
    }

    if (expected_len_out)
        *expected_len_out = expected;

    if (expected > 256)
        return -1;

    if (len < expected)
        return 0;

    if (!modbus_rtu_crc_ok(buf, expected))
        return -1;

    return 1;
}

// r0 has the byte to output, hook is at 0x80008c0
void fuzz_xMBPortSerialPutByte(unsigned int cpu_index, void *udata)
{
    static uint8_t modbus_out[256];
    static size_t out_size = 0;

    (void)cpu_index;
    (void)udata;

    uint8_t mb_byte = (uint8_t)fuzz_get_register(0);

    printf(" -> %02x\n", mb_byte);

    return;

    if (out_size >= sizeof(modbus_out)) {
        perror("Ran out of room of outgoing buffer, larger than modbus sizes");
        out_size = 0;
        return;
    }

    modbus_out[out_size++] = mb_byte;

    size_t expected_len = 0;
    int done = modbus_rtu_response_done(modbus_out, out_size, &expected_len);

    if (done == 1) {
        fuzz_set_data((char *)modbus_out, expected_len);

        /*
         * Usually output should be exactly one RTU response.
         * If extra bytes somehow arrived, preserve them as start of next frame.
         */
        if (out_size > expected_len) {
            memmove(modbus_out,
                    modbus_out + expected_len,
                    out_size - expected_len);
            out_size -= expected_len;
        } else {
            out_size = 0;
        }
    } else if (done == 0) {
        // nothing for now
    } else if (done < 0) {
        out_size = 0;
    }
}
