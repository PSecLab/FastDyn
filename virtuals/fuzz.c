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
#include "core.h"
#include "common.h"

extern int coverage;

uint32_t last_anchor_id = -1;

static char g_fuzzing_buf[1024];
static char g_fuzzing_input[2048];

void fuzz_report_assert(uint32_t anchor_id, bool fatal);
void fuzz_finish(uint32_t anchor_id);
int fuzz_buffer_read(uint32_t anchor_id, char* out, size_t len);

// static void vcpu_tb_exec(unsigned int vcpu_index, void *userdata)
// {
//     // userdata is whatever you passed at registration time
// }

void virt_assert(unsigned int cpu_index, void *udata)
{
    if (!coverage) {
        utils_die("[assert] Coverage not enabled, cannot assert coverage data");
    }

    if (!udata)
        return;

    printf("[assert] Asserted\n");

    const char *str = (const char *)udata;

    // Expect something like "*0x8003940"
    if (str[0] != '*') {
        fprintf(stderr, "[assert] Invalid format: %s\n", str);
        return;
    }

    // Skip the '*' and parse the rest as hex or decimal
    uint64_t addr = strtoull(str + 1, NULL, 0);
    if (addr != 0) { // set pc to supplied address
        fuzz_report_assert(last_anchor_id, false);
        qemu_set_register(addr, ARM_V7M_PC);
    } else { // for address == 0, perform a reset
        uint32_t msp = qemu_get_register(13);

        if (msp != 0) {
            qemu_plugin_read_memory(msp, (uint8_t*)g_fuzzing_buf, sizeof(uint32_t) * 8);

            printf("[assert] Hard fault:\n");
            for (int i = 0; i < 8; i++) {
                printf("[assert] Stack[%d] = %x\n", i, ((uint32_t*)g_fuzzing_buf)[i]);
            }
        }

        fuzz_report_assert(last_anchor_id, true);
        fuzz_finish(last_anchor_id);
        // wait for fuzzer to observe the crash
        while (true);
    }
}

void anchor(unsigned int cpu_index, void *udata)
{
    // Currently reserving 0xDEADBEEF for a crash, we should have a better systems
    // For example, all exceptions?
    if (!coverage) {
        utils_die("[anchor] Coverage not enabled, cannot assert coverage data");
    }
    if (!udata) return;

    const char *input_str = (const char *)udata;
	//TODO: Fix this buffer thing
    strncpy(g_fuzzing_buf, input_str, sizeof(g_fuzzing_buf) - 1);
    g_fuzzing_buf[sizeof(g_fuzzing_buf) - 1] = '\0';

    // Split into filename and numbers
    char *anchor_id = strtok(g_fuzzing_buf, ":");
    if (anchor_id == NULL) {
        utils_die("[anchor] Couldn't get the anchor id from string");
    }
    char *numbers = strtok(NULL, ":");
    if (numbers == NULL) {
        utils_die("[anchor] Couldn't get the target registers/memory from string");
    }

    if (last_anchor_id != -1) {
        fuzz_finish(last_anchor_id);
    }
    last_anchor_id = strtoul(anchor_id, NULL, 0);

    uint32_t read_count = fuzz_buffer_read(last_anchor_id, g_fuzzing_input, sizeof(g_fuzzing_input));

    // Parse each number
    int idx = 0;
    char *token = strtok(numbers, ",");
    while (token && (idx + 4) <= read_count) {
        unsigned long value = strtoul(token, NULL, 0);
        if (value < 100) {
            //vale < 100 means its a register number to write to
            uint32_t write_value = 0;
            memcpy(&write_value, g_fuzzing_input + idx, 4);
            //printf("Writing %x to %d at pc = %x, lr = %x\n", write_value, value, qemu_get_register(15), qemu_get_register(14));
            qemu_set_register(write_value, value);
        } else if (value < 0xFFFFFFFF) {
            //value of 0xFFFFFFFF means do nothing, less than that means write to address
            qemu_plugin_write_memory(value, (uint8_t *)&g_fuzzing_input[idx], 4);
        }
        idx +=4;
        token = strtok(NULL, ",");
    }
}