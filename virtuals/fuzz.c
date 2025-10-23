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


static uint64_t *get_iteration_fuzz_counter(void)
{
    int fd;
    uint64_t *fuzz_counter;

    fd = open(SHM_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("open");
        return NULL;
    }

    // Ensure file is large enough for one 64-bit integer
    if (ftruncate(fd, sizeof(uint64_t)) < 0) {
        perror("ftruncate");
        close(fd);
        return NULL;
    }

    fuzz_counter = mmap(NULL, 2 *sizeof(uint64_t),
                   PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, 0);
    close(fd); // mapping is now independent of fd

    if (fuzz_counter == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    return fuzz_counter;
}
int fuzzer_init_done = 0;
uint64_t *fuzz_counter = NULL;
uint64_t *input_counter = NULL;
void anchor(unsigned int cpu_index, void *udata)
{
    // Currently reserving 0xDEADBEEF for a crash, we should have a better systems
    // For example, all exceptions?
    if (!coverage) {
        utils_die("Coverage not enabled, cannot assert coverage data");
    }
    if (!udata) return;

	// Dump coverage 
	reset_and_dump_values(NULL);

    if (!fuzzer_init_done) {
        fuzz_counter = get_iteration_fuzz_counter();
		input_counter = fuzz_counter+1;
		*fuzz_counter = 0;
		*input_counter =0;
        fuzzer_init_done = 1;
    } else {
		*fuzz_counter = *fuzz_counter + 1;
	}

	//Wait for next input
	while (*input_counter <= *fuzz_counter);


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

    // Read entire file
	//TODO: The size of buf needs to be fixed
	FILE *fp = fopen(filename, "rb");
	if (!fp) {
	    perror("fopen");
	    exit(1);
	}

    size_t read_count = fread(buf, 1, 1024, fp);
    fclose(fp);


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
				printf("[anchor] Not enough random bytes");
				break;
		}
        token = strtok(NULL, ",");
    }
}
