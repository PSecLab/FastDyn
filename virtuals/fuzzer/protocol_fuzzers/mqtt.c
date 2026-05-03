#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "core.h"
#include "common.h"
#include "fuzz.h"

#define MQTT_MAX_SIZE 65536 // the firmware treats size as a uint16
static char mqtt_buf[MQTT_MAX_SIZE];

static size_t buf_pointer = 0;
static size_t buf_top = 0;

static int mqtt_encode_remaining_length(uint32_t length, uint8_t *buf)
{
    if (length > 268435455)  // 256 MB max per MQTT spec
        return -1;

    int i = 0;
    do {
        uint8_t encoded = length % 128;
        length /= 128;

        if (length > 0)
            encoded |= 0x80;  // set continuation bit

        buf[i++] = encoded;
    } while (length > 0 && i < 4);

    return i;
}

// r0 = our return status, useful if something fails we can fail the transport 0 = success, else = fail
// r1 = pointer to the mqtt buffer
// r2 = 16 bit size of the mqtt buffer

// copies fuzzed data into mqtt_buf with a fixed length check since that is required to even insert the data.
// the length header is variable-length, so we receive the data 4 bytes in so we only have to move the 1 byte header down.
void fuzz_mqtt_in(unsigned int cpu_index, void *udata)
{
    static long long prints = 0;
    if (buf_top >= buf_pointer) {
        char size_buf[4];
        //printf("1 - %lld\n", prints++);
        size_t rd = fuzz_get_data(&mqtt_buf[4], sizeof(mqtt_buf) - 4);
        //printf("2 - %lld\n", prints++);

        if (rd == 0) {
            fuzz_set_register(-1, 0);
            return;
        }

        int shift = mqtt_encode_remaining_length(rd - 1, size_buf);

        if (shift < 0) {
            fuzz_set_register(-1, 0);
            return;
        }

        mqtt_buf[4 - shift] = mqtt_buf[4]; // copy the header byte into the correct spot
        memcpy(&mqtt_buf[5 - shift], size_buf, shift); // copy the variable-length len after the payload

        buf_top = rd + 4;
        buf_pointer = 4 - shift;
    }

    //printf("3 - %lld\n", prints++);

    uint32_t req_buf = fuzz_get_register(1);
    uint32_t req_size = fuzz_get_register(2);

    if (req_size > buf_top - buf_pointer) {
        fuzz_set_register(-1, 0);
        return;
    }

    fuzz_write_memory(req_buf, &mqtt_buf[buf_pointer], req_size);

    buf_pointer += req_size;

    fuzz_set_register(0, 0);
}