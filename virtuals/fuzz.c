#include <utils.h>
#include <core.h>
#include <common.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define SHM_PATH "/tmp/iteration_count"
extern int coverage;

static uint32_t **p_observed_values = NULL;
static size_t *p_observed_count = NULL;

// Opaque handle from Rust
typedef struct FuzzHandle FuzzHandle;

// Create the fuzzer thread and return a handle
FuzzHandle* fuzz_init(void);

// Receive a 4‑byte input from Rust (blocks until available)
// Returns 1 on success, 0 on failure
uint32_t fuzz_receive_input(FuzzHandle* handle, uint32_t* out_input);

// Send a list of PCs (each 4 bytes) back to Rust
// Returns 1 on success, 0 on failure
uint32_t fuzz_submit_pcs(FuzzHandle* handle, const uint32_t* pcs, uint32_t pcs_len);

// Shut down the fuzzer and free the handle
void fuzz_free(FuzzHandle* handle);

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
        fprintf(stderr, "[anchor] Invalid format: %s\n", str);
        return;
    }

    // Skip the '*' and parse the rest as hex or decimal
    uint64_t addr = strtoull(str + 1, NULL, 0);
    qemu_set_register(addr, 15);


}

int fuzzer_init_done = 0;
void anchor(unsigned int cpu_index, void *udata)
{
    // Reserve handle for fuzzer
    static FuzzHandle* hFuzzer = NULL;

    // Currently reserving 0xDEADBEEF for a crash, we should have a better systems
    // For example, all exceptions?
    if (!coverage) {
        utils_die("Coverage not enabled, cannot assert coverage data");
    }
    if (!udata) return;

    if (!fuzzer_init_done) {
        hFuzzer = fuzz_init();
        p_observed_values = get_pobserved_values();
        p_observed_count = get_pobserved_count();
	} else {
        // Dump coverage, but only after an initial fuzz
        if (!fuzz_submit_pcs(hFuzzer, *p_observed_values, *p_observed_count)) { 
            utils_die("Rust side closed PC channel");
        }
        *p_observed_count = 0;
    }

    if (!hFuzzer || !p_observed_values || !p_observed_count) {
        utils_die("Failed initialization, or freed before finished");
    }

    // 2- Fuzz
    const char *input_str = (const char *)udata;
	//TODO: Fix this buffer thing
    char buf[1024];
    strncpy(buf, input_str, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    // Split into filename and numbers
    char *filename = strtok(buf, ":");
    char *numbers = strtok(NULL, ":");

    if (!filename || !numbers) {
        fprintf(stderr, "[anchor] Invalid input format: %s\n", input_str);
        return;
    }

    printf("[anchor] CPU %u, file: %s\n", cpu_index, filename);

    uint32_t read_count;
    if (!fuzz_receive_input(hFuzzer, &read_count)) {
        utils_die("Fuzzer couldn't read input");
        return;
    }

    printf("Reading %u inputs\n", read_count);

    int idx = 0;
    // Parse each number
    char *token = strtok(numbers, ",");
    while (token) {
        unsigned long value = strtoul(token, NULL, 0);
        uint32_t fuzzed_input;
        if (!fuzz_receive_input(hFuzzer, &fuzzed_input)) {
            utils_die("Fuzzer couldn't read input");
            return;
        }
        if (value < 100) {
            qemu_set_register(fuzzed_input, value);
        } else {
            qemu_plugin_write_memory(value, fuzzed_input, 4);
        }
        idx +=1;
		if (idx >= read_count) {
				printf("[anchor] Not enough random bytes");
				break;
		}
        token = strtok(NULL, ",");
        printf("idx %d\n", idx);
    }
    // clear out unneeded input
    while (idx < read_count) {
        uint32_t temp;
        if (!fuzz_receive_input(hFuzzer, &temp)) {
            utils_die("Fuzzer couldn't read input");
            return;
        }

        idx += 1;
    }

    printf("Done reading!\n");
}
