#include <stdint.h>
#include <stddef.h>

typedef unsigned char uint8_t;
typedef void (*fuzz_callback_t)();

size_t fuzz_get_data(char* buf, size_t len);
uint32_t fuzz_get_register(int reg);
void fuzz_set_register(uint32_t value, int reg);
int fuzz_write_memory(unsigned long long addr, uint8_t *mem_buf, int len);
int fuzz_read_memory(unsigned long long addr, uint8_t *mem_buf, int len);

#define UBLOX_PAYLOAD_OFFSET 0x62
#define UBLOX_PAYLOAD_SIZE   1040

static uint8_t fuzz_buf[UBLOX_PAYLOAD_SIZE];

void fuzz_callback()
{
    size_t i;
    uint32_t r0;
    unsigned long long base;

    for (i = 0; i < sizeof(fuzz_buf); i++) {
        fuzz_buf[i] = 0;
    }

    (void)fuzz_get_data((char *)fuzz_buf, sizeof(fuzz_buf));

    r0 = fuzz_get_register(0);

    base = (unsigned long long)r0;
    if (base == 0) {
        return;
    }

    /*
     * _parse_gps() consumes the completed UBX payload at offset 0x62 in the
     * AP_GPS_UBLOX object.  Leave the packet class, id, and length fields
     * captured by the snapshot intact so the parser receives a valid frame
     * shape while the payload itself is fuzzed.
     */
    (void)fuzz_write_memory(base + UBLOX_PAYLOAD_OFFSET,
                            fuzz_buf, (int)sizeof(fuzz_buf));
}
