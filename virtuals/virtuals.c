#include <ctype.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>
#include <stdatomic.h>

#include <qemu/qemu-plugin.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <utils.h>
#include <device.h>
#if ENABLE_LIBPY
#include <python.h>
#endif
#include <virtuals.h>
#include "introspection/inspct.h"
// #include "ardupilot_virtuals.c"
#if ENABLE_LIBGZ
    #include "phy.h"
#endif

#include "introspection/inspct.h"

// External dependencies from core.c
extern AddressList addressLists[];
extern size_t listCount;
extern uint32_t qemu_get_register(int reg);
extern void qemu_set_register(uint32_t value, int reg);

// Global to track last sim time from QEMU
_Atomic int64_t last_sim_time_ns = 0;

//TODO: remove this.
#if ENABLE_LIBPY
#include <Python.h>
#include "../python/python.c"
#endif

#include <virtuals/inspct.h>
#include <virtuals/fuzz.h>
#include <virtuals/phy.h>

#define VIRTUALS_MAX_COUNT 256

// Track how many are currently used
static int registry_count = -1;
/**
 * @brief Callback registry
 *
 * To register a new callback, add a new entry to this list,
 * declare the function in virtuals.h and implement it here.
 *
 */
cb_entry_t cb_registry[VIRTUALS_MAX_COUNT] = {
	{ "printreg", printreg },
    { "updatemem", updatemem},
    { "randstate", randstate},
    { "raiseirq", raiseirq },
    { "pulseirq", pulseirq },
    { "dumplog", dumplogger},
    { "dyninst", dyninst},
    { "timer_start", timer_start},
    { "start_budgeting", start_budgeting},
    { "dyninst_lib", dyninst_lib},
    { "debug_log", debug_log},
    { "raise_periodic_irq", raise_periodic_irq},
	{NULL, NULL} //This should always be last entry.
};


// Function to lookup callbacks by name (moved from core.c)
cb_func_t lookup_callback(const char *name) {
    for (size_t i = 0; i < registry_count; i++) {
        if (strcmp(cb_registry[i].name, name) == 0)
            return cb_registry[i].func;
    }
    return NULL;
}

// Timer callback
static void my_timer_callback(void *opaque) {
    printf("Virtual Clock: %li\n", qemu_plugin_get_virtual_timer());
}

// Virtual instruction functions
void raiseirq(unsigned int cpu_index, void *udata) {
	 // Interpret udata as a string
    const char *str = (const char *)udata;

    // Convert string to integer (auto-detect base: 0x = hex, 0 = octal, else decimal)
    unsigned long num = strtoul(str, NULL, 0);

    qemu_plugin_raise_irq(num, false);
}

void kick_irq(void *opaque) {
    int irq_num = *((int *)opaque);

#if ENABLE_LIBGZ
    // update the sim time global in ardurover_virtuals.c
    atomic_store(&last_sim_time_ns, (int64_t)qemu_plugin_get_virtual_timer());
    // // TODO:
    // static bool physics_initialized = false;
    // if (!physics_initialized) {
    //     if (!phy_select_backend("gazebo")) {
    //         fprintf(stderr, "Failed to initialize physics backend\n");
    //         exit(EXIT_FAILURE);
    //     }
    //     physics_initialized = true;
    // }
#endif

    qemu_plugin_raise_irq(irq_num, false);
}

// void periodic_irq_func(void *arg) {
//     int irq_num = *((int *)arg);

//     while (1) {
//         kick_irq((void *)&irq_num);
//         usleep(100); // Sleep for 0.1 ms
//     }
// }

void raise_periodic_irq(unsigned int cpu_index, void *udata) {
    // Interpret udata as a string
    const char *str = (const char *)udata;

    // Convert string to integer (auto-detect base: 0x = hex, 0 = octal, else decimal)
    unsigned long num = strtoul(str, NULL, 0);
    static uint32_t irq_num = 0;
    if (num != 0) {
        irq_num = num;
    }
    qemu_plugin_timer_new_period_ns(kick_irq, (void *)&irq_num, 1e6); // every 1 ms
    // create thread that raises irq every 1 ms
    // pthread_t periodic_irq_thread;
    // if (!pthread_create(&periodic_irq_thread, NULL, (void *(*)(void *))periodic_irq_func, (void *)&irq_num)) {
    //     pthread_detach(periodic_irq_thread);
    // } else {
    //     fprintf(stderr, "Failed to create periodic IRQ thread\n");
    // }
}

void pulseirq(unsigned int cpu_index, void *udata) {
	// Interpret udata as a string
    const char *str = (const char *)udata;

    // Convert string to integer (auto-detect base: 0x = hex, 0 = octal, else decimal)
    unsigned long num = strtoul(str, NULL, 0);
    qemu_plugin_pulse_irq(num);
}

void updatemem(unsigned int cpu_index, void *udata) {
    const char *input = (const char *) udata;
    MemAccess mem;

    if (parse_update_mem_arg(input, &mem) == 0) {
        if (mem.mode == 'r') {
            qemu_plugin_read_memory(mem.address, mem.buffer, mem.length);
            printf("Read %u bytes from 0x%08x: ", mem.length, mem.address);
            for (uint32_t i = 0; i < mem.length; i++) {
                printf("0x%02x", mem.buffer[i]);
                if (i < mem.length - 1) {
                    printf(", ");
                }
            }
            printf("\n");
        } else {
            qemu_plugin_write_memory(mem.address, mem.buffer, mem.length);
        }
    }
}

unsigned char get_random_byte(void) {
    //This will make things linux specific, but lot of hardcoded things.
    FILE *fp = fopen("/dev/urandom", "rb");
    if (!fp) {
        perror("fopen /dev/urandom");
        exit(EXIT_FAILURE);
    }

    unsigned char byte;
    size_t result = fread(&byte, 1, 1, fp);
    fclose(fp);

    if (result != 1) {
        fprintf(stderr, "Failed to read from /dev/urandom\n");
        exit(EXIT_FAILURE);
    }

    return byte;
}

uint32_t get_random_word(void) {
    //This will make things linux specific, but lot of hardcoded things.
    FILE *fp = fopen("/dev/urandom", "rb");
    if (!fp) {
        perror("fopen /dev/urandom");
        exit(EXIT_FAILURE);
    }

    uint32_t word;
    size_t result = fread(&word, sizeof(uint32_t), 1, fp);
    fclose(fp);

    if (result != 1) {
        fprintf(stderr, "Failed to read from /dev/urandom\n");
        exit(EXIT_FAILURE);
    }

    return word;
}

void randstate(unsigned int cpu_index, void *udata) {
    const char *input = (const char *) udata;

    size_t count = 0;
    unsigned long long *addrs = parse_addresses(input, &count);
    if (addrs) {
        for (size_t i = 0; i < count; i++) {
            if (addrs[i] < 100) {
                uint32_t val = get_random_word();
                //PC not supported
                if (addrs[i] != 15) {
                    qemu_plugin_set_register((uint8_t *)&val, addrs[i]);
                }
            } else {
                uint8_t val = get_random_byte();
                qemu_plugin_write_memory(addrs[i], &val, 1);
            }
        }
        free(addrs);
    } else {
        printf("Failed to parse addresses.\n");
    }
}

void debug_log(unsigned int cpu_index, void *udata) {
    // print sim time from qemu

    uint64_t sim_time_ns = qemu_plugin_get_virtual_timer();
    double fractional_seconds = (double)(sim_time_ns % 1000000000) / 1e9;

    const char *msg = (const char *)udata;

    printf("[%u][%.6f] : %s\n", cpu_index, fractional_seconds, msg);
}

void printreg(unsigned int cpu_index, void *udata) {
	unsigned long reg = strtoul((char *)(udata), NULL, 16);
	printf("Reg %lu:0x%08x\n", reg, qemu_get_register(reg));
}

void timer_start(unsigned int cpu_index, void *udata) {
    const char *msg = "Hello from QEMU timer!";
#if 00
    //One shot
    uint64_t timer = qemu_plugin_timer_new_ns(my_timer_callback, (void *)msg);
    qemu_plugin_timer_alarm(timer, 1e6);
#else
    //Periodic
    qemu_plugin_timer_new_period_ns(my_timer_callback, (void *)msg, 1e6);
#endif
}

void start_budgeting(unsigned int cpu_index, void *udata) {
    qemu_plugin_wait_for_budget();
}

void dyninst_lib(unsigned int cpu_index, void *udata) {
    qemu_plugin_load_elf((char *) udata);
}

void dyninst(unsigned int cpu_index, void *udata) {
    AddrFilePair parsed = parse_addr_file((char *)udata);

    size_t file_len = 0;
    void *file_buf = read_file(parsed.filename, &file_len);
    if (file_buf) {
        qemu_plugin_write_memory(parsed.addr, file_buf, file_len);
        free(file_buf);
    }
}

void dumplogger(unsigned int cpu_index, void *udata) {
    FileEntry entry;
    parse_file_entry((const char*) udata, &entry);
    dump_log_buffer_to_file(&addressLists[entry.idx], entry.file_name);
}

unsigned long long* parse_addresses(const char *input, size_t *count) {
    // Make a copy of input so we don't modify the original
    char *input_copy = strdup(input);
    if (!input_copy) return NULL;

    size_t capacity = 8;
    *count = 0;
    unsigned long long *addresses = malloc(capacity * sizeof(unsigned long long));
    if (!addresses) {
        free(input_copy);
        return NULL;
    }

    char *token = strtok(input_copy, ",");
    while (token) {
        // Remove leading/trailing whitespace
        while (*token == ' ' || *token == '\t') token++;
        char *endptr;
        unsigned long long addr = strtoull(token, &endptr, 0);
        if (token == endptr) {
            // Invalid conversion
            free(addresses);
            free(input_copy);
            return NULL;
        }

        if (*count >= capacity) {
            capacity *= 2;
            addresses = realloc(addresses, capacity * sizeof(unsigned long long));
            if (!addresses) {
                free(input_copy);
                return NULL;
            }
        }

        addresses[(*count)++] = addr;
        token = strtok(NULL, ",");
    }

    free(input_copy);
    return addresses;
}

int parse_update_mem_arg(const char *input, MemAccess *out) {
    if (!input || !out) return -1;

    // Temporary copy of input string for tokenizing
    char *temp = malloc(strlen(input) + 1);
    if (!temp) return -1;
    strcpy(temp, input);

    char *token = strtok(temp, ":");
    if (!token) { free(temp); return -1;}
    out->address = strtoul(token, NULL, 0); // parse address

    token = strtok(NULL, ":");
    if (!token || (token[0] != 'r' && token[0] != 'w')) return -1;
    out->mode = token[0]; // parse mode

    token = strtok(NULL, ":");
    if (!token) { free(temp); return -1;}
    out->length = strtoul(token, NULL, 0); // parse length
    if (out->length > MAX_BUFFER_SIZE) return -1;

    token = strtok(NULL, ":");
    if (!token) { free(temp); return -1;}

    // Now parse comma-separated bytes
    uint32_t i = 0;
    char *byte_str = strtok(token, ",");
    while (byte_str && i < out->length) {
        out->buffer[i++] = (uint8_t)strtoul(byte_str, NULL, 0);
        byte_str = strtok(NULL, ",");
    }

    if (i != out->length) { printf("Invalid Argument \n"); free(temp); return -1;}

    free(temp);
    return 0; // success
}

AddrFilePair parse_addr_file(const char *input) {
    AddrFilePair result = {0, {0}};

    const char *colon = strchr(input, ':');
    if (!colon) {
        fprintf(stderr, "Invalid format: no ':' found.\n");
        return result;
    }

    // Parse address part
    char addr_str[32] = {0}; // Enough for 64-bit address string
    size_t addr_len = colon - input;

    if (addr_len >= sizeof(addr_str)) {
        fprintf(stderr, "Address string too long.\n");
        return result;
    }

    strncpy(addr_str, input, addr_len);
    addr_str[addr_len] = '\0';

    result.addr = strtoull(addr_str, NULL, 0); // auto-detect 0x

    // Copy filename part into fixed buffer
    const char *filename = colon + 1;

    if (strlen(filename) >= MAX_FILENAME_LEN) {
        fprintf(stderr, "Filename too long. Truncated.\n");
        strncpy(result.filename, filename, MAX_FILENAME_LEN - 1);
        result.filename[MAX_FILENAME_LEN - 1] = '\0'; // Null-terminate
    } else {
        strcpy(result.filename, filename);
    }

    return result;
}

void* read_file(const char *filename, size_t *length) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        return NULL;
    }

    // Seek to end to find file size
    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Error seeking file");
        fclose(file);
        return NULL;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        perror("Error telling file position");
        fclose(file);
        return NULL;
    }
    rewind(file); // Go back to start

    // Allocate buffer
    void *buffer = malloc(file_size);
    if (!buffer) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // Read entire file into buffer
    size_t read_size = fread(buffer, 1, file_size, file);
    if (read_size != file_size) {
        perror("Error reading file");
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *length = file_size; // Return size
    return buffer;
}

bool parse_file_entry(const char* line, FileEntry* entry) {
    char* colonPos = strchr(line, ':');
    if (!colonPos) {
        return false; // No colon found — invalid format
    }

    // Split into index and file_name parts
    size_t idxLen = colonPos - line;
    char idxStr[32]; // Enough for int
    if (idxLen >= sizeof(idxStr)) return false; // Index too big to fit

    strncpy(idxStr, line, idxLen);
    idxStr[idxLen] = '\0';

    // Parse integer index
    entry->idx = atoi(idxStr);

    // Copy file name part
    strncpy(entry->file_name, colonPos + 1, sizeof(entry->file_name) - 1);
    entry->file_name[sizeof(entry->file_name) - 1] = '\0'; // Ensure null-terminated

    return true;
}

void dump_log_buffer_to_file(const AddressList* list, const char* filename) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        perror("fopen");
        return;
    }

    // Write the entire buffer
    size_t written = fwrite(list->log_buf.buffer, sizeof(uint32_t), (LOG_BUFFER_SIZE/sizeof(uint32_t)), file);
    if (written != LOG_BUFFER_SIZE) {
        fprintf(stderr, "Warning: Only wrote %zu words out of %u\n", written, LOG_BUFFER_SIZE);
    }

    fclose(file);
}


/**
 * Scans the static portion of the array to find the
 * initial count of hardcoded functions.
 */
static int virtuals_init_registry() {
    if (registry_count != -1) return -1;

    registry_count = 0;
    while (registry_count < VIRTUALS_MAX_COUNT &&
           cb_registry[registry_count].name != NULL) {
        registry_count++;
    }

	return 0;
}

/**
 * Registers a new function at runtime or updates an existing one.
 */
int virtual_register(const char *name, cb_func_t func) {
	virtuals_init_registry();

    if (!name || !func) return -1;

    // 1. Check if we're updating an existing entry
    for (int i = 0; i < registry_count; i++) {
        if (strcmp(cb_registry[i].name, name) == 0) {
			fprintf(stderr, "Registry Error: Entry '%s' already exists.\n", name);
            return -1; // Registration rejected
        }
    }

    // 2. Check for capacity (leaving room for a trailing sentinel)
    if (registry_count >= VIRTUALS_MAX_COUNT - 1) {
        fprintf(stderr, "Error: VIRTUALS_MAX_COUNT reached\n");
        return -1;
    }

    // 3. Append the new virtual
    cb_registry[registry_count].name = name;
    cb_registry[registry_count].func = func;
    registry_count++;

    // 4. Move the sentinel forward
    cb_registry[registry_count].name = NULL;
    cb_registry[registry_count].func = NULL;

    return 0;
}

int virtuals_init(int argc, char **argv, const char *schema_path) {
	int status = -1;
	// Initialize registry
	status = virtuals_init_registry();

	if (status == -1) {
			utils_die("Couldn't init registry");
	}

    if (status != -1) {
		// Initialize subcomponents, each compnoent can fail independently so no reason to stop initiliaztion if one fails
		if ((status = inspct_init(argc, argv, schema_path)) < 0)
				utils_warn("Introspection failed");
#if ENABLE_PHY
		if ((status = phy_init(argc, argv)) < 0)
				utils_warn("Physics Engine failed");
#endif
#if ENABLE_LIBFUZZ
		if ((status = fuzz_init(argc, argv)) < 0)
				utils_warn("Fuzzer Failed");
#endif
	}

    return 0;
}
