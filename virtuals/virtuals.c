#include <ctype.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>
#include <Python.h>

#include <qemu-plugin.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "common.h"
#include <utils.h>
#include <device.h>
#include <python.h>
#include <virtuals.h>

#include "gazebo_wrapper.h"

// External dependencies from core.c
extern AddressList addressLists[];
extern size_t listCount;
extern uint32_t qemu_get_register(int reg);
extern void qemu_set_register(uint32_t value, int reg);

// Global variables for Python integration
static uint8_t py_init = false;
static PyObject *fastdyn_interceptor = NULL;
static PyObject *halucinator_initialize = NULL;

/**
 * @brief Callback registry
 * 
 * To register a new callback, add a new entry to this list,
 * declare the function in virtuals.h and implement it here.
 * 
 */
cb_entry_t cb_registry[] = {
    { "updatepc", updatepc },
    { "updatereg", updatereg},
    { "updatemem", updatemem},
    { "randstate", randstate},
    { "raiseirq", raiseirq },
    { "pulseirq", pulseirq },
    { "dumplog", dumplogger},
    { "dyninst", dyninst},
    { "timer_start", timer_start},
    { "start_budgeting", start_budgeting},
    { "dyninst_lib", dyninst_lib},
    { "fastdyn_callback", fastdyn_callback},
    { "gz_service", gz_service},
};

const size_t cb_registry_len = sizeof(cb_registry) / sizeof(cb_registry[0]);

// Function to lookup callbacks by name (moved from core.c)
cb_func_t lookup_callback(const char *name) {
    for (size_t i = 0; i < cb_registry_len; i++) {
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
    qemu_plugin_raise_irq(15);
}

void pulseirq(unsigned int cpu_index, void *udata) {
    qemu_plugin_pulse_irq(15);
}

void updatemem(unsigned int cpu_index, void *udata) {
    const char *input = (const char *) udata;
    MemAccess mem;

    if (parse_update_mem_arg(input, &mem) == 0) {
        if (mem.mode == 'r') {
            qemu_plugin_read_memory(mem.address, mem.buffer, mem.length);
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

void updatepc(unsigned int cpu_index, void *udata) {
    const char *s = (const char *) udata;
    if (s[0] != '*') {
        printf("Wrong usage of CF affecting virtual function \n");
        return;
    }
    unsigned long addr = strtoul((s + 1), NULL, 16);

    qemu_plugin_set_register((uint8_t *) &addr, ARM_V7M_PC);
}

void updatereg(unsigned int cpu_index, void *udata) {
    FloatConverter fc;
    fc.f = 3.14;
    qemu_set_register(fc.i, ARM_V7M_S0);
    DoubleConverter dc;
    dc.d = 3.14;
    qemu_plugin_set_register((uint8_t *)&dc.i, ARM_V7M_D4);
    fc.i = qemu_get_register(ARM_V7M_S0);
    DEBUG_LOG("Hello %f \n", fc.f);
}

void timer_start(unsigned int cpu_index, void *udata) {
    const char *msg = "Hello from QEMU timer!";
#if 01
    //One shot
    int timer = qemu_plugin_timer_new_ns(my_timer_callback, (void *)msg);
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

void fastdyn_callback(unsigned int cpu_index, void *udata) {
    const char *input = (const char *) udata;
    if (!py_init) {
        //Initialize the Python Interpreter
        Py_Initialize();

        PyRun_SimpleString("import sys");
        PyRun_SimpleString("import os");
        PyRun_SimpleString("sys.stdout = os.fdopen(sys.stdout.fileno(), 'w', buffering=1)");
        PyRun_SimpleString("sys.stderr = os.fdopen(sys.stderr.fileno(), 'w', buffering=1)");
        PyRun_SimpleString("sys.path.append('.')");

        //Load the module for the C APIs <-> Python Interaction.
        PyObject *hal_reg_mem = PyUnicode_FromString("src.halucinator.hal_reg_mem");
        PyObject *hal_reg_mem_module = PyImport_Import(hal_reg_mem);
        Py_DECREF(hal_reg_mem);

        if (!hal_reg_mem_module) {
            PyErr_Print();
            fprintf(stderr, "Failed to load Python script.\n");
        }

        Py_XDECREF(hal_reg_mem_module);

        //Qemu -> Halucinator (Single Process)
        //Let's initialize the Halucinator Hal_initialzer -> For now, the configurations are defined inside the file, will update later, once stable.
        PyObject *Halucinator = PyUnicode_FromString("src.halucinator.main");
        PyObject *Halucinator_module = PyImport_Import(Halucinator);

        //Intercept Function to be called once the qemu starts halucinator. (initialize once)
        PyObject *Intercepts_file = PyUnicode_FromString("src.halucinator.bp_handlers.intercepts");
        PyObject *Intercepts_module = PyImport_Import(Intercepts_file);

        Py_DECREF(Halucinator);
        Py_DECREF(Intercepts_file);

        if (Halucinator_module != NULL && Intercepts_module != NULL) {
            //Get the Halucinator Initializer function
            halucinator_initialize = PyObject_GetAttrString(Halucinator_module, "halucinator_initialize");

            //Get the intecptor function from the intercepts module.
            fastdyn_interceptor = PyObject_GetAttrString(Intercepts_module, "intercept_fastdyn_callback");

            //verify the existance of the halucinator_initialize
            if (halucinator_initialize && PyCallable_Check(halucinator_initialize)) {
                //TODO: Improve this logic :>
                //Empyty block, don't do anything, just want to catch errors here
            } else {
                //Add garbage handling mabye when we go out of scope
                Py_DECREF(Halucinator_module);
                PyErr_Print();
                exit(1);
            }

            if (fastdyn_interceptor && PyCallable_Check(fastdyn_interceptor)) {
                py_init = true;
            } else {
                //Add garbage handling mabye when we go out of scope
                Py_DECREF(Intercepts_module);
                PyErr_Print();
                exit(1);
            }
            Py_DECREF(Halucinator_module);
            Py_DECREF(Intercepts_module);
        } else {
            PyErr_Print();
            exit(1);
        }

        //Let's initialize Halucinator only once!
        //No arguments, handled by halucinator itself
        //TODO: In future, find a way to pass arguments from here?
        PyObject *halucinator_initialize_args = PyTuple_Pack(0);

        // Call the Halucinator Initialize Function
        PyObject *Halucinator_return_val = PyObject_CallObject(halucinator_initialize, halucinator_initialize_args);

        Py_DECREF(halucinator_initialize_args);
        //Verify the halucinator was initialized successfully!
        if (Halucinator_return_val != NULL && PyTuple_Check(Halucinator_return_val)){
            PyObject *hal_return_val = PyTuple_GetItem(Halucinator_return_val, 0);  // True/False -> show whether halucinator was initialized successfully or not!

            int arg1 = PyObject_IsTrue(hal_return_val);    // Converts True/False to 1/0
            if (arg1){
                printf("Successfuly initialized Halucinator...");
            } else {
                Py_DECREF(Halucinator_return_val);
                printf("Error Initializing the Halucinator! Exiting...");
                exit(1);
            }
            Py_DECREF(Halucinator_return_val);
        } else {
            PyErr_Print();
            exit(1);
        }
    }
    if (py_init) {
        DEBUG_LOG("fastdyn api called!\n");
        DEBUG_LOG("input pc: %s\n",input);

        //Build the arguments. -> PC Value passed by the user when registering the callback!
        PyObject *fastdyn_callback_args = PyTuple_Pack(1, PyUnicode_FromString(input));

        // Call the Initialize function
        PyObject *fastdyn_callback_return_val = PyObject_CallObject(fastdyn_interceptor, fastdyn_callback_args);

        Py_DECREF(fastdyn_callback_args);
        //Verify the halucinator was initialized successfully!
        if (fastdyn_callback_return_val != NULL && PyTuple_Check(fastdyn_callback_return_val)){
            Py_DECREF(fastdyn_callback_return_val);
        } else {
            PyErr_Print();
            exit(1);
        }
    }
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

void gz_service(unsigned int cpu_index, void *udata) {
	static float yaw = 0.0;
    if (set_rover_pose(yaw) == 0) {
        printf("Pose set successfully\n");
		yaw += 30.0;
	} else {
        printf("Failed to set pose\n");
    }
}

// Helper functions that need to be implemented or declared
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

int virtuals_init(int argc, char **argv) {
    return 0;
}
