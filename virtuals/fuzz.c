#include <utils.h>
#include <core.h>
#include <common.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

extern int coverage;

// Create the fuzzer thread and return a handle
void *fuzz_get(uint32_t id, char *numbers);

// Reads input bytes from rust, returns bytes read
uint32_t fuzz_receive_input(void *handle, char* buf, uint32_t len);

// Shut down the fuzzer and free the handle
void fuzz_free(void *handle);

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
    add_observed_value(0xDEADBEEF);

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

void* prev_fuzzer = NULL;
int fuzzer_init_done = 0;
void anchor(unsigned int cpu_index, void *udata)
{
    // Currently reserving 0xDEADBEEF for a crash, we should have a better systems
    // For example, all exceptions?
    if (!coverage) {
        utils_die("Coverage not enabled, cannot assert coverage data");
    }
    if (!udata) return;

    // dump coverage, clearing coverage if fuzzer hasn't started yet
    if (!dump_trace_info(prev_fuzzer)) { 
        utils_die("Rust side closed PC channel");
    }

    const char *input_str = (const char *)udata;
	//TODO: Fix this buffer thing
    char buf[1024];
    strncpy(buf, input_str, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    // Split into filename and numbers
    char *anchor_id = strtok(buf, ":");
    char *numbers = strtok(NULL, ":");

    // 2- Fuzz
    if (!anchor_id || !numbers) {
        fprintf(stderr, "[anchor] Invalid input format: %s\n", input_str);
        return;
    }

    // Reserve handle for fuzzer
    void *hFuzzer = fuzz_get(strtoul(anchor_id, NULL, 0), numbers);
    if (!hFuzzer) {
        utils_die("Failed initialization, or freed before finished");
    }

    prev_fuzzer = hFuzzer;

    printf("[anchor] CPU %u, id: %s\n", cpu_index, anchor_id);

    uint32_t read_count = fuzz_receive_input(hFuzzer, buf, 1024) + 1; // +1 since originally was written for counting the null terminator
    int idx = 0;

    // Parse each number
    char *token = strtok(numbers, ",");
    while (token) {
        unsigned long value = strtoul(token, NULL, 0);
        if (value < 100) {
            qemu_set_register(*(uint32_t*)(buf +idx), value);
        } else {
            qemu_plugin_write_memory(value, (uint8_t *)&buf[idx], 4);
        }
        idx +=4;
		if (idx >= read_count) {
            printf("[anchor] not enough bytes\n");
            break;
		}
        token = strtok(NULL, ",");
    }
}
