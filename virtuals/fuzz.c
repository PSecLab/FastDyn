#include <utils.h>
#include <core.h>
#include <common.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

extern int coverage;

uint32_t last_anchor_id = -1;

static void vcpu_tb_exec(unsigned int vcpu_index, void *userdata)
{
    // userdata is whatever you passed at registration time
}

void virt_assert(unsigned int cpu_index, void *udata)
{
    if (!coverage) {
        utils_die("Coverage not enabled, cannot assert coverage data");
    }

    if (!udata)
        return;

    // Currently reserving 0xDEADBEEF for a crash, we should have a better systems
    // For example, all exceptions?
    fuzz_report_assert(last_anchor_id);

    const char *str = (const char *)udata;

    // Expect something like "*0x8003940"
    if (str[0] != '*') {
        fprintf(stderr, "[assert] Invalid format: %s\n", str);
        return;
    }

    // Skip the '*' and parse the rest as hex or decimal
    uint64_t addr = strtoull(str + 1, NULL, 0);
    qemu_set_register(addr, 15);
}

static char g_fuzzing_buf[1024];
static char g_fuzzing_input[1024];

void anchor(unsigned int cpu_index, void *udata)
{
    // Currently reserving 0xDEADBEEF for a crash, we should have a better systems
    // For example, all exceptions?
    if (!coverage) {
        utils_die("Coverage not enabled, cannot assert coverage data");
    }
    if (!udata) return;

    // Previous anchor done now that we've reached another
    if (last_anchor_id != -1) {
        fuzz_finish(last_anchor_id);
    }

    const char *input_str = (const char *)udata;
	//TODO: Fix this buffer thing
    strncpy(g_fuzzing_buf, input_str, sizeof(g_fuzzing_buf) - 1);
    g_fuzzing_buf[sizeof(g_fuzzing_buf) - 1] = '\0';

    // Split into filename and numbers
    char *anchor_id = strtok(g_fuzzing_buf, ":");
    if (anchor_id == NULL) {
        utils_die("Couldn't get the anchor id from string");
    }
    char *numbers = strtok(NULL, ":");
    if (numbers == NULL) {
        utils_die("Couldn't get the target registers/memory from string");
    }

    last_anchor_id = strtoul(anchor_id, NULL, 0);

    //printf("[anchor] CPU %u, id: %d\n", cpu_index, last_anchor_id);

    uint32_t read_count = fuzz_buffer_read(last_anchor_id, g_fuzzing_input, 1024);
    memset(g_fuzzing_input + read_count, 0, ((read_count + 3) & ~3) - read_count); // zero pad up to a 4 byte aligned size

    // Parse each number
    int idx = 0;
    char *token = strtok(numbers, ",");
    while (token && read_count) {
        unsigned long value = strtoul(token, NULL, 0);
        if (value < 100) {
            //qemu_set_register(try_this_value++, 0);
            uint32_t write_value = 0;
            memcpy(&write_value, g_fuzzing_input + idx, 4);
            qemu_set_register(write_value, value);
        } else {
            qemu_plugin_write_memory(value, (uint8_t *)&g_fuzzing_input[idx], 4);
        }
        idx +=4;
		if (idx >= read_count) { // no more bytes left, whether or not there are more fuzzing targets
            break;
		}
        token = strtok(NULL, ",");
    }
}
