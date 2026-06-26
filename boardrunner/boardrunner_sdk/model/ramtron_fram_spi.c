// SPI Slave implementation: ramtron_fram_spi.c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/spi.h>

#define RAMTRON_CMD_WREN  0x06
#define RAMTRON_CMD_WRITE 0x02
#define RAMTRON_CMD_READ  0x03
#define RAMTRON_CMD_RDSR  0x05
#define RAMTRON_CMD_RDID  0x9F

#define RAMTRON_SIZE_BYTES (512U * 1024U)
#define RAMTRON_ADDR_LEN   3U

typedef enum {
    RAMTRON_MODE_IDLE = 0,
    RAMTRON_MODE_RDID,
    RAMTRON_MODE_STATUS,
    RAMTRON_MODE_READ_ADDR,
    RAMTRON_MODE_READ_DATA,
    RAMTRON_MODE_WRITE_ADDR,
    RAMTRON_MODE_WRITE_DATA
} RamtronMode;

typedef struct {
    uint8_t mem[RAMTRON_SIZE_BYTES];
    int storage_fd;
    bool initialized;
    bool selected;
    bool write_enable;
    bool dirty;
    RamtronMode mode;
    uint32_t addr;
    uint8_t addr_bytes_seen;
    uint8_t rdid_idx;
    uint32_t data_bytes_seen;
} RamtronState;

static RamtronState g_ramtron;

/*
 * Cypress-compatible RDID matching AP_RAMTRON's table:
 *   manufacturer[6] = 7F 7F 7F 7F 7F C2
 *   memory          = 00
 *   id1,id2         = 26 08   (CY15B104Q, 512KB, 3-byte address)
 *
 * The STM32/ChibiOS SPI path used by AP_RAMTRON sends the RDID opcode first,
 * then consumes the returned identification bytes on the following dummy
 * clocks. Return a dummy byte during the opcode phase and stream the 9-byte
 * RDID payload afterwards; this also works for split command-then-read
 * sequences under one chip-select window.
 */
static const uint8_t g_ramtron_rdid[] = {
    0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0xC2, 0x00, 0x26, 0x08
};

static bool ramtron_is_opcode(uint8_t value) {
    switch (value) {
    case RAMTRON_CMD_WREN:
    case RAMTRON_CMD_WRITE:
    case RAMTRON_CMD_READ:
    case RAMTRON_CMD_RDSR:
    case RAMTRON_CMD_RDID:
        return true;
    default:
        return false;
    }
}

static void ramtron_reset_session(RamtronState *s) {
    s->mode = RAMTRON_MODE_IDLE;
    s->addr = 0;
    s->addr_bytes_seen = 0;
    s->rdid_idx = 0;
    s->data_bytes_seen = 0;
}

static uint8_t ramtron_status(const RamtronState *s) {
    uint8_t status = 0;

    if (s->write_enable) {
        status |= (1U << 1);
    }

    return status;
}

static uint8_t ramtron_begin_command(RamtronState *s, uint8_t opcode) {
    s->addr = 0;
    s->addr_bytes_seen = 0;
    s->rdid_idx = 0;
    s->data_bytes_seen = 0;

    switch (opcode) {
    case RAMTRON_CMD_RDID:
        s->mode = RAMTRON_MODE_RDID;
        s->rdid_idx = 0;
        return 0;

    case RAMTRON_CMD_RDSR:
        s->mode = RAMTRON_MODE_STATUS;
        return 0;

    case RAMTRON_CMD_WREN:
        s->write_enable = true;
        s->mode = RAMTRON_MODE_IDLE;
        return 0;

    case RAMTRON_CMD_READ:
        s->mode = RAMTRON_MODE_READ_ADDR;
        return 0;

    case RAMTRON_CMD_WRITE:
        s->mode = RAMTRON_MODE_WRITE_ADDR;
        return 0;

    default:
        s->mode = RAMTRON_MODE_IDLE;
        return 0;
    }
}

static void ramtron_finish_write_stream(RamtronState *s) {
    if (s->mode == RAMTRON_MODE_WRITE_DATA) {
        s->write_enable = false;
    }
}

static void ramtron_init_once(RamtronState *s) {
    uint64_t file_size = 0;

    if (s->initialized) {
        return;
    }

    memset(s, 0, sizeof(*s));
    s->storage_fd = -1;

    s->storage_fd = api_file_open("fram_storage.bin", 1);
    if (s->storage_fd >= 0) {
        file_size = api_file_get_size(s->storage_fd);
        if (file_size == 0) {
            memset(s->mem, 0xFF, sizeof(s->mem));
            api_file_pwrite(s->storage_fd, s->mem, sizeof(s->mem), 0);
        } else {
            api_file_pread_fill(s->storage_fd, s->mem, sizeof(s->mem), 0, 0xFF);
            if (file_size < sizeof(s->mem)) {
                api_file_pwrite(s->storage_fd, s->mem, sizeof(s->mem), 0);
            }
        }
    } else {
        memset(s->mem, 0xFF, sizeof(s->mem));
    }

    s->initialized = true;
    s->selected = false;
    s->write_enable = false;
    s->dirty = false;
    ramtron_reset_session(s);
}

uint32_t slave_spi_transfer(uint32_t value) {
    RamtronState *s = &g_ramtron;
    uint8_t in = (uint8_t)(value & 0xFF);

    ramtron_init_once(s);

    /*
     * Command framing is defined by chip-select. Do not infer a new command
     * from opcode-looking payload bytes: AP_Param records legitimately contain
     * values such as 0x06 and 0x02, and treating them as in-band command
     * boundaries truncates FRAM writes.
     */
    switch (s->mode) {
    case RAMTRON_MODE_IDLE:
        return ramtron_begin_command(s, in);

    case RAMTRON_MODE_RDID: {
        uint8_t out = 0;

        if (s->rdid_idx < sizeof(g_ramtron_rdid)) {
            out = g_ramtron_rdid[s->rdid_idx++];
        }
        if (s->rdid_idx >= sizeof(g_ramtron_rdid)) {
            ramtron_reset_session(s);
        }
        return out;
    }

    case RAMTRON_MODE_STATUS:
        return ramtron_status(s);

    case RAMTRON_MODE_READ_ADDR:
        s->addr = (s->addr << 8) | in;
        s->addr_bytes_seen++;
        if (s->addr_bytes_seen >= RAMTRON_ADDR_LEN) {
            s->addr %= RAMTRON_SIZE_BYTES;
            s->mode = RAMTRON_MODE_READ_DATA;
            s->data_bytes_seen = 0;
        }
        return 0;

    case RAMTRON_MODE_READ_DATA: {
        uint8_t out = s->mem[s->addr % RAMTRON_SIZE_BYTES];
        s->addr = (s->addr + 1U) % RAMTRON_SIZE_BYTES;
        s->data_bytes_seen++;
        return out;
    }

    case RAMTRON_MODE_WRITE_ADDR:
        s->addr = (s->addr << 8) | in;
        s->addr_bytes_seen++;
        if (s->addr_bytes_seen >= RAMTRON_ADDR_LEN) {
            s->addr %= RAMTRON_SIZE_BYTES;
            s->mode = RAMTRON_MODE_WRITE_DATA;
            s->data_bytes_seen = 0;
        }
        return 0;

    case RAMTRON_MODE_WRITE_DATA:
        if (s->write_enable) {
            s->mem[s->addr % RAMTRON_SIZE_BYTES] = in;
            s->addr = (s->addr + 1U) % RAMTRON_SIZE_BYTES;
            s->dirty = true;
        }
        s->data_bytes_seen++;
        return 0;

    default:
        ramtron_reset_session(s);
        return 0;
    }
}

void slave_spi_set_cs(int level) {
    RamtronState *s = &g_ramtron;

    ramtron_init_once(s);

    s->selected = (level == 0);

    if (level != 0) {
        if (s->storage_fd >= 0 && s->dirty) {
            api_file_pwrite(s->storage_fd, s->mem, sizeof(s->mem), 0);
            s->dirty = false;
        }
        ramtron_finish_write_stream(s);
        ramtron_reset_session(s);
    }
}