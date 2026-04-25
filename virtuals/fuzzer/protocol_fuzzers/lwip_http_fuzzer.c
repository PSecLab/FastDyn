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
#include <immintrin.h>
#include <virtuals/virt_fuzz.h>

#include "core.h"
#include "common.h"
#include "fuzz.h"

void fuzz_plugin_lwip_http_fuzzer(char *buff, size_t len)
{
    static uint32_t pbuf = 0;
    static uint32_t pbuf_payload = 0;
    static uint16_t pbuf_len = 0;

    if (pbuf == 0) pbuf = fuzz_get_register(2);
    if (pbuf == 0) {
        printf("http_recv arg is null");
        return;
    }

    // get pointer to payload and available space
    if (pbuf_payload == 0) fuzz_read_memory(pbuf + 4, &pbuf_payload, 4);
    if (pbuf_len == 0) fuzz_read_memory(pbuf + 10, &pbuf_len, 2);

    // sample correct message for testing
    // const char *http_sample = "GET /leds.cgi?led=1 HTTP/1.0\r\n\r\n";
    // len = strlen(http_sample) + 1;
    // buff = (char*)http_sample;

    uint16_t len16 = (uint16_t)len;

    // write our payload and length to firmware's internal object
    fuzz_write_memory(pbuf_payload, buff, len);
    fuzz_write_memory(pbuf + 8, (uint8_t *)&len16, 2);
    fuzz_write_memory(pbuf + 10, (uint8_t *)&len16, 2);

    return;
}