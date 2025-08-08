/*
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
int isdigit(int c);
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "core.h"
#include <utils.h>
#include <device.h>
#include <python.h>

#define MAX_ENTRIES 1024
#define MAX_LINE_LEN 128
#define MAX_RULES 256

typedef enum {
    VALUE_IMMEDIATE, // existing: value is immediate
    VALUE_REGISTER,  // new: value comes from another register (e.g., r3)
    VALUE_DEREF      // new: value is loaded from the memory address held in r3
} ValueType;

typedef enum {
    TARGET_REGISTER,
    TARGET_MEMORY,
	TARGET_DEREF
} TargetType;

typedef struct {
    unsigned long update_point;
    TargetType type; // TARGET_REGISTER or TARGET_MEMORY

    union {
        int reg_num;          // if TARGET_REGISTER
        unsigned long addr;   // if TARGET_MEMORY
    } target;

    ValueType value_type;

    union {
        unsigned long imm; // VALUE_IMMEDIATE
        int reg_num;       // VALUE_REGISTER and VALUE_DEREF (source register)
    } value;
} UpdateEntry;

static const char * runtime;

#define MAX_LISTS 100
#define MAX_ENTRIES_PER_LIST 100
#define LINE_BUFFER_SIZE 1024

typedef struct {
    uint32_t *buffer;  // Pointer to the buffer
    uint16_t index;     // Current index into the buffer
} Buffy;

typedef struct {
    uintptr_t address;
    int reg; // "register" number (just a number)
} LoggerEntry;

typedef struct {
    LoggerEntry entries[MAX_ENTRIES_PER_LIST];
    size_t count;
	Buffy log_buf;
} AddressList;

AddressList addressLists[MAX_LISTS];
size_t listCount = 0;

// Helper: Parse "0xADDR:REGISTER" format
bool parse_entry(const char* token, LoggerEntry* entry);
bool parse_entry(const char* token, LoggerEntry* entry) {
    char* colonPos = strchr(token, ':');
    if (!colonPos) return false;

    *colonPos = '\0';
    const char* addrPart = token;
    const char* regPart = colonPos + 1;

    // Parse address
    uintptr_t addr = (uintptr_t)strtoull(addrPart, NULL, 0);

    // Parse register (as a simple integer)
    int regNum = atoi(regPart);

    entry->address = addr;
    entry->reg = regNum;
    return true;
}

// Parse a single line into an AddressList
void parse_logger(const char* line);
void parse_logger(const char* line) {
    if (listCount >= MAX_LISTS) {
        fprintf(stderr, "Too many lists!\n");
        return;
    }

    AddressList* list = &addressLists[listCount];
    list->count = 0;

    char* lineCopy = strdup(line);
    if (!lineCopy) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    char* token = strtok(lineCopy, ",\n\r");
    while (token != NULL && list->count < MAX_ENTRIES_PER_LIST) {
        while (*token == ' ' || *token == '\t') token++; // trim leading whitespace

        LoggerEntry entry;
        if (parse_entry(token, &entry)) {
            list->entries[list->count++] = entry;
        } else {
            fprintf(stderr, "Invalid entry: %s\n", token);
        }

        token = strtok(NULL, ",\n\r");
    }

    free(lineCopy);
    listCount++;
}

// Load logger configuration file
void load_logger_config(const char* filename);
void load_logger_config(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    char buffer[LINE_BUFFER_SIZE];
    while (fgets(buffer, sizeof(buffer), file)) {
        parse_logger(buffer);
    }

    fclose(file);
}

// Check if address+register exists in any list
bool address_in_any_list(uintptr_t addr, int reg);
bool address_in_any_list(uintptr_t addr, int reg) {
    for (size_t i = 0; i < listCount; i++) {
        AddressList* list = &addressLists[i];
        for (size_t j = 0; j < list->count; j++) {
            if (list->entries[j].address == addr && list->entries[j].reg == reg) {
                return true;
            }
        }
    }
    return false;
}
rule_t rules[MAX_RULES];
size_t rules_count = 0;

// Global storage
UpdateEntry update_entries[MAX_ENTRIES];
size_t update_entry_count = 0;



// Helper to parse a single line
static int parse_update_line(const char *line, UpdateEntry *entry);
int parse_update_line(const char *line, UpdateEntry *entry) {
    char buf[128];
    char *token;
    char *endptr;

    strncpy(buf, line, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0'; // Ensure null termination

    // First token: update_point
    token = strtok(buf, " \t");
    if (!token) return -1;
    entry->update_point = strtoul(token, &endptr, 0);
    if (*endptr != '\0') return -1;

    // Second token: target (rX, [rX] or 0xADDRESS)
	token = strtok(NULL, " \t");
    if (!token) return -1;
	if (token[0] == 'r') {
    entry->type = TARGET_REGISTER;
    entry->target.reg_num = strtoul(token + 1, &endptr, 0);
    if (*endptr != '\0') return -1;
	} else if (token[0] == '[' && token[strlen(token) - 1] == ']') {
    // Target is [rX] dereference
    token[strlen(token) - 1] = '\0'; // Remove trailing ']'
    if (token[1] != 'r') {
        fprintf(stderr, "Invalid target deref syntax: %s\n", token);
        return -1;
    }
    entry->type = TARGET_DEREF;
    entry->target.reg_num = strtoul(token + 2, &endptr, 0); // skip [r
    if (*endptr != '\0') return -1;
	} else if (strncmp(token, "0x", 2) == 0) {
    entry->type = TARGET_MEMORY;
    entry->target.addr = strtoul(token, &endptr, 0);
    if (*endptr != '\0') return -1;
	} else {
    fprintf(stderr, "Invalid target: %s\n", token);
    return -1;
	}


    // Third token: value (immediate, register, or dereference)
    token = strtok(NULL, " \t");
    if (!token) return -1;

    if (token[0] == 'r') {
        // Source is a register value
        entry->value_type = VALUE_REGISTER;
        entry->value.reg_num = strtoul(token + 1, &endptr, 0);
        if (*endptr != '\0') return -1;
    } else if (token[0] == '[' && token[strlen(token) - 1] == ']') {
        // Source is [rX] dereference
        token[strlen(token) - 1] = '\0'; // strip trailing ']'
        if (token[1] != 'r') {
            fprintf(stderr, "Invalid dereference syntax: %s\n", token);
            return -1;
        }
        entry->value_type = VALUE_DEREF;
        entry->value.reg_num = strtoul(token + 2, &endptr, 0); // skip [r
        if (*endptr != '\0') return -1;
    } else {
        // Must be an immediate
        entry->value_type = VALUE_IMMEDIATE;
        entry->value.imm = strtoul(token, &endptr, 0);
        if (*endptr != '\0') return -1;
    }

    return 0;
}

// Function to load all updates from a file into the global array
int load_update_entries(const char *filename);
int load_update_entries(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        return 0;
    }

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), f)) {
        // Strip newline
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '\0' || line[0] == '#') continue;

        if (update_entry_count >= MAX_ENTRIES) {
            fprintf(stderr, "Too many entries (limit: %d)\n", MAX_ENTRIES);
            fclose(f);
            return 0;
        }

        if (parse_update_line(line, &update_entries[update_entry_count]) == 0) {
            update_entry_count++;
        } else {
            fprintf(stderr, "Error parsing line: %s\n", line);
        }
    }

    fclose(f);
    return 1;
}

#define MAX_TUPLES 1000

typedef struct {
    uintptr_t anchor;
    uintptr_t target;
} AddressTuple;

static AddressTuple address_tuples[MAX_TUPLES];
static size_t num_tuples = 0;

/* Prototypes */
AddressTuple * is_target_address(uintptr_t addr);
void print_tuples(AddressTuple *tuples, size_t count);
size_t read_tuples_from_file(const char *filename, AddressTuple *tuples, size_t max_tuples);

AddressTuple * is_target_address(uintptr_t addr) {
    for (size_t i = 0; i < num_tuples; ++i) {
        if (address_tuples[i].target == addr) {
            return &address_tuples[i];
        }
    }
    return NULL;
}

size_t read_tuples_from_file(const char *filename, AddressTuple *tuples, size_t max_tuples) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        exit(1);
    }

    size_t count = 0;
    while (count < max_tuples &&
           fscanf(f, "%lx %lx", &tuples[count].anchor, &tuples[count].target) == 2) {
        count++;
    }

    fclose(f);
    return count;
}

void print_tuples(AddressTuple *tuples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        printf("Tuple %zu: %p -> %p\n", i,
               (void *)tuples[i].anchor,
               (void *)tuples[i].target);
    }
}
/* You can use these functions like this

int main() {
    AddressTuple tuples[MAX_TUPLES];

    size_t count = read_tuples_from_file("addrs.txt", tuples, MAX_TUPLES);
    print_tuples(tuples, count);

    return 0;
}

*/


#define MAX_BUFFER_SIZE 256

typedef struct {
    uint32_t address;
    char mode; // 'r' or 'w'
    uint32_t length;
    uint8_t buffer[MAX_BUFFER_SIZE];
} MemAccess;


int parse_update_mem_arg(const char *input, MemAccess *out);
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

unsigned long long* parse_addresses(const char *input, size_t *count);
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



QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
int counter;

#define HOOK_POINT	(0x106d6)
#define ANCHOR		(0x106cc)


static void raiseirq(unsigned int cpu_index, void *udata);
static void pulseirq(unsigned int cpu_index, void *udata);
static void updatepc(unsigned int cpu_index, void *udata);
static void updatereg(unsigned int cpu_index, void *udata);
static void updatemem(unsigned int cpu_index, void *udata);
static void randstate(unsigned int cpu_index, void *udata);
static void dumplogger(unsigned int cpu_index, void *udata);
static void dyninst(unsigned int cpu_index, void *udata);
static void dyninst_lib(unsigned int cpu_index, void *udata);
static void fastdyn_callback(unsigned int cpu_index, void *udata);
static void timer_start(unsigned int cpu_index, void *udata);
static void start_budgeting(unsigned int cpu_index, void *udata);
PyMODINIT_FUNC PyInit_emb(void);
uint32_t qemu_get_register(int reg);
uint32_t qemu_get_register(int reg)
{
    g_autoptr(GArray) reg_list = qemu_plugin_get_registers();
    g_autoptr(GByteArray) reg_value = g_byte_array_new();
	int offset = 0;
	int oreg = reg;

	if (reg >= ARM_V7M_S0)
		oreg = 17 + ((reg - ARM_V7M_S0) / 2);


    if (reg_list) {
            qemu_plugin_reg_descriptor *rd = &g_array_index(
                reg_list, qemu_plugin_reg_descriptor, oreg);
            int count = qemu_plugin_read_register(rd->handle, reg_value);
            g_assert(count > 0);
    }

	if ((reg >= ARM_V7M_S0) && ((reg - ARM_V7M_S0)  %2)) {
			//S1...
			offset = 4;
	}

    uint32_t return_data = reg_value->data[offset + 0];
    return_data = (((uint32_t) (reg_value->data[offset + 1])) << 8)  | return_data;
    return_data = (((uint32_t) (reg_value->data[offset + 2])) << 16) | return_data;
    return_data = (((uint32_t) (reg_value->data[offset + 3])) << 24) | return_data;
    return return_data;
}

uint64_t core_get_pc(void) {
	uint64_t ret_val;
	ret_val = qemu_get_register(ARM_V7M_PC);
	return ret_val;
}

void qemu_set_register(uint32_t value, int reg);
void qemu_set_register(uint32_t value, int reg) {
	if ((reg >= ARM_V7M_S0)) {
			DoubleConverter dc;
			dc.i[((reg - ARM_V7M_S0)  %2)] = value;
			dc.i[(((reg - ARM_V7M_S0)  %2) + 1) %2] = 0;
			reg = ARM_V7M_S0 + ((reg - ARM_V7M_S0) / 2);
			qemu_plugin_set_register((uint8_t *)&dc, reg);
    } else {
			qemu_plugin_set_register((uint8_t *)&value, reg);
	}
}


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
};

static void my_timer_callback(void *opaque) {
    printf("Virtual Clock: %li\n", qemu_plugin_get_virtual_timer());
}
static void timer_start(unsigned int cpu_index, void *udata) {
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

static void start_budgeting(unsigned int cpu_index, void *udata) {
	qemu_plugin_wait_for_budget();
}



#define MAX_FILENAME_LEN 256

typedef struct {
    uint64_t addr;
    char filename[MAX_FILENAME_LEN];  // Fixed-size buffer
} AddrFilePair;

static void dyninst_lib(unsigned int cpu_index, void *udata) {
	qemu_plugin_load_elf((char *) udata);
}

static uint8_t py_init = false;
static PyObject *fastdyn_interceptor = NULL;
static PyObject *halucinator_initialize = NULL;
void fastdyn_callback(unsigned int cpu_index, void *udata) {
    // uint32_t val;
    // uint32_t r0_val;
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


//Expose read/write registers/memory API from here...
uint32_t read_reg(int reg);
uint32_t read_reg(int reg){
    uint32_t reg_val = qemu_get_register(reg);
    return reg_val;
}
static PyObject *read_reg_callback(PyObject *self, PyObject *args) {
    return PyCapsule_New((void *)read_reg, "read_reg_func", NULL);
}

uint32_t read_floating_reg(int reg);
uint32_t read_floating_reg(int reg){
    FloatConverter fc;
    fc.i = qemu_get_register(reg);
    DEBUG_LOG("Read_REG value %f\n", fc.f);
    return fc.f;
}
static PyObject *read_floating_reg_callback(PyObject *self, PyObject *args) {
    return PyCapsule_New((void *)read_floating_reg, "read_floating_reg_func", NULL);
}

void write_reg(int reg, uint32_t val);
void write_reg(int reg, uint32_t val){
    qemu_set_register(val, reg);
}
static PyObject *write_reg_callback(PyObject *self, PyObject *args) {
    return PyCapsule_New((void *)write_reg, "write_reg_func", NULL);
}

void write_floating_reg(int reg, float val);
void write_floating_reg(int reg, float val){
    FloatConverter fc;
    fc.f = val;
    DEBUG_LOG("the value from c code is %f\n", fc.f);
    qemu_set_register(fc.i, reg);
}
static PyObject *write_floating_reg_callback(PyObject *self, PyObject *args) {
    return PyCapsule_New((void *)write_floating_reg, "write_floating_reg_func", NULL);
}

int read_memory(unsigned long long addr, uint8_t *mem_buf, int len);
int read_memory(unsigned long long addr, uint8_t *mem_buf, int len){
    return qemu_plugin_read_memory(addr, mem_buf, len);
}
static PyObject *read_mem_callback(PyObject *self, PyObject *args) {
    return PyCapsule_New((void *)read_memory, "read_mem_func", NULL);
}

int write_memory(unsigned long long addr, uint8_t *mem_buf, int len);
int write_memory(unsigned long long addr, uint8_t *mem_buf, int len){
    return qemu_plugin_write_memory(addr, mem_buf, len);
}
static PyObject *write_mem_callback(PyObject *self, PyObject *args) {
    return PyCapsule_New((void *)write_memory, "write_mem_func", NULL);
}

unsigned long long virtual_clock(void);
unsigned long long virtual_clock(void){
    return (unsigned long long)qemu_plugin_get_virtual_timer();
}

static PyObject *virtual_clock_callback(PyObject *self, PyObject *args) {
    return PyCapsule_New((void *)virtual_clock, "virtual_clock_func", NULL);
}

// Python method definitions for both read and write
static PyMethodDef EmbMethods[] = {
    {"read_reg_callback", read_reg_callback, METH_NOARGS, "Returns a pointer to the C read_reg callback function"},
    {"write_reg_callback", write_reg_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
    {"read_mem_callback", read_mem_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
    {"write_mem_callback", write_mem_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
    {"virtual_clock_callback", virtual_clock_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
    {"write_floating_reg_callback", write_floating_reg_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
    {"read_floating_reg_callback", read_floating_reg_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef qemuapi = {
    PyModuleDef_HEAD_INIT, "qemuapi", NULL, -1, EmbMethods
};

PyMODINIT_FUNC PyInit_emb(void) {
    return PyModule_Create(&qemuapi);
}


AddrFilePair parse_addr_file(const char *input);
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

// Reads entire file into a buffer.
// Returns pointer to buffer and sets *length to file size.
// Returns NULL on error.
void* read_file(const char *filename, size_t *length);
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

void dyninst(unsigned int cpu_index, void *udata) {
	AddrFilePair parsed = parse_addr_file((char *)udata);

	size_t file_len = 0;
	void *file_buf = read_file(parsed.filename, &file_len);
	if (file_buf) {
		qemu_plugin_write_memory(parsed.addr, file_buf, file_len);
		free(file_buf);
	}
}

#define LOG_BUFFER_SIZE (UINT16_MAX + 1)

const size_t cb_registry_len = sizeof(cb_registry) / sizeof(cb_registry[0]);
void dump_log_buffer_to_file(const AddressList* list, const char* filename);
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
typedef struct {
    int idx;
    char file_name[256]; // Max file name size (adjust as needed)
} FileEntry;

bool parse_file_entry(const char* line, FileEntry* entry);
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

static void dumplogger(unsigned int cpu_index, void *udata) {
	FileEntry entry;
	parse_file_entry((const char*) udata, &entry);
	dump_log_buffer_to_file(&addressLists[entry.idx], entry.file_name);
}

static void raiseirq(unsigned int cpu_index, void *udata){
	qemu_plugin_raise_irq(15);
}

static void pulseirq(unsigned int cpu_index, void *udata){
    qemu_plugin_pulse_irq(15);
}

static void updatemem(unsigned int cpu_index, void *udata) {
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

#include <stdio.h>
#include <stdlib.h>

unsigned char get_random_byte(void);
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
uint32_t get_random_word(void);
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


static void randstate(unsigned int cpu_index, void *udata) {
	const char *input = (const char *) udata;

	size_t count = 0;
	unsigned long long *addrs = parse_addresses(input, &count);
	if (addrs) {
        for (size_t i = 0; i < count; i++) {
			if (addrs[i] < 100) {
				uint32_t val = get_random_word();
				//PC not supported
				if (addrs[i] != 15) {
					qemu_plugin_set_register((uint8_t *)&val,addrs[i] );
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
static void updatepc(unsigned int cpu_index, void *udata)
{
	const char *s = (const char *) udata;
	if (s[0] != '*') {
			printf("Wrong usage of CF affecting virtual function \n");
			return;
	}
	unsigned long addr = strtoul((s + 1), NULL, 16);

	qemu_plugin_set_register((uint8_t *) &addr, ARM_V7M_PC);
}

static void updatereg(unsigned int cpu_index, void *udata)
{
	FloatConverter fc;
	fc.f = 3.14;
    qemu_set_register(fc.i, ARM_V7M_S0);
	DoubleConverter dc;
	dc.d = 3.14;
	qemu_plugin_set_register((uint8_t *)&dc.i, ARM_V7M_D4);
	fc.i = qemu_get_register(ARM_V7M_S0);
	DEBUG_LOG("Hello %f \n", fc.f);
}



// Finds all update entries targeting the given memory address.
// `matches` is an output array to be filled with pointers to matching entries.
// `max_matches` limits how many results to return.
// Returns the number of matches found.
size_t find_updates_for_address(unsigned long addr,
                                UpdateEntry **matches,
                                size_t max_matches);
size_t find_updates_for_address(unsigned long addr,
                                UpdateEntry **matches,
                                size_t max_matches)
{
    size_t count = 0;
    for (size_t i = 0; i < update_entry_count && count < max_matches; ++i) {
        if (update_entries[i].update_point == addr)
        {
            matches[count++] = &update_entries[i];
        }
    }
    return count;
}

int inline_ins = 0;
#define MAX_MATCHES 10


typedef struct {
    AddressList* list;
    LoggerEntry* entry;
} LookupResult;

// Find an address in any list and return both the list and entry pointers
LookupResult lookup_addr(uintptr_t addr);
LookupResult lookup_addr(uintptr_t addr) {
    LookupResult result = {0};

    for (size_t i = 0; i < listCount; i++) {
        AddressList* list = &addressLists[i];
        for (size_t j = 0; j < list->count; j++) {
            if (list->entries[j].address == addr) {
                result.list = list;
                result.entry = &list->entries[j];
                return result; // First match returned
            }
        }
    }

    // Not found
    result.list = NULL;
    result.entry = NULL;
    return result;
}
static int init = 0;
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
	if (runtime && !init) {
			qemu_plugin_load_elf((char *)runtime);
			init = 1;
	}
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;
	UpdateEntry *matches[MAX_MATCHES];

	DEBUG_LOG("->Virtual Clock: %llu \n", (unsigned long long)qemu_plugin_get_virtual_timer());

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

		if (qemu_plugin_insn_vaddr(insn) == 0x20800050) {
				//Magic instruction
				qemu_plugin_u64 entry_tmp;
                // In TCG frontend it is already set, if you want to modify it you will have to
                // change CPSR.
                entry_tmp.data = NULL;
				qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_UPDATE_REG, entry_tmp, -1);
				return;
		}


		//Highest priority: Logger
		LookupResult ret = lookup_addr(qemu_plugin_insn_vaddr(insn));
		if (ret.list) {
			qemu_plugin_u64 entry_tmp;
			if (!ret.list->log_buf.buffer) {
					ret.list->log_buf.buffer = malloc(UINT16_MAX + 1);
			}
			entry_tmp.offset = (size_t)&ret.list->log_buf;
			qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_LOG_REG, entry_tmp, ret.entry->reg);
		}


		//Second Highest prioirity is Virtual instructions
		rule_t  *rule;
        if (find_rule_by_address(qemu_plugin_insn_vaddr(insn), &rule)) {
				if (rule->args[0] == '*') {
					qemu_plugin_register_vcpu_insn_exec_cb(
                        insn, rule->func, QEMU_PLUGIN_CB_RW_REGS | QEMU_PLUGIN_CB_RW_CFI, rule->args);
				} else {
                	qemu_plugin_register_vcpu_insn_exec_cb(
                    	insn, rule->func, QEMU_PLUGIN_CB_RW_REGS, rule->args);
				}
        }

		//Third priority: Modifier
		//void * handle= qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn,  QEMU_PLUGIN_CB_GEN_LABEL, NULL, 0);
		size_t count = find_updates_for_address(qemu_plugin_insn_vaddr(insn), matches, MAX_MATCHES);
		if (count > 0) {
		for (size_t match_idx = 0; match_idx < count; ++match_idx) {
			UpdateEntry *e = matches[match_idx];

			DEBUG_LOG("  Update Point: 0x%lx, ", e->update_point);
	        if (e->type == TARGET_REGISTER || e->type == TARGET_DEREF) {
    	        DEBUG_LOG("Target: r%d, ", e->target.reg_num);
				qemu_plugin_u64 entry;
                // In TCG frontend it is already set, if you want to modify it you will have to
                // change CPSR.
                entry.offset = (size_t)(e->value.imm);
				entry.data = (void *)e;
                qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_UPDATE_REG, entry, e->target.reg_num);
	        } else if (e->type == TARGET_MEMORY) {
				DEBUG_LOG("Target: r%d, ", e->target.reg_num);
                qemu_plugin_u64 entry;
                // In TCG frontend it is already set, if you want to modify it you will have to
                // change CPSR.
                entry.offset = (size_t)(e->value.imm);
                qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_UPDATE_MEM, entry, e->target.addr);
    	        DEBUG_LOG("Target: 0x%lx, ", e->target.addr);
       		}

    	}
		}

		//Lowest priority is detour
		AddressTuple * tuple = is_target_address(qemu_plugin_insn_vaddr(insn));
        if (tuple) {
                qemu_plugin_u64 entry;
                // In TCG frontend it is already set, if you want to modify it you will have to
                // change CPSR.
                entry.offset = (tuple->anchor & ~(0x1));
                qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_UPDATE_REG, entry, 15);
        }
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
}

static cb_func_t lookup_callback(const char *name) {
    for (size_t i = 0; i < cb_registry_len; i++) {
        if (strcmp(cb_registry[i].name, name) == 0)
            return cb_registry[i].func;
    }
    return NULL;
}


bool find_rule_by_address(unsigned long long addr, rule_t **out_rule) {
    for (size_t i = 0; i < rules_count; i++) {
        if (rules[i].address == addr) {
            if (out_rule) {
                *out_rule = &rules[i];
            }
            return true;
        }
    }
    return false;
}

void parse_rules_file(const char *filename);
void parse_rules_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Failed to open rules file");
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '#') continue;
        line[strcspn(line, "\r\n")] = 0;

        char addr_str[32];
        char cb_name[64];
        char args[301] = {0};

        int n = sscanf(line, "%31s %63s %300[^\n]", addr_str, cb_name, args);
        if (n < 2) {
            fprintf(stderr, "Invalid line in rules file: '%s'\n", line);
            continue;
        }

        cb_func_t cb = lookup_callback(cb_name);
        if (!cb) {
            fprintf(stderr, "Error: Callback '%s' not found in registry (line: '%s')\n", cb_name, line);
            continue;
        }

        if (rules_count >= MAX_RULES) {
            fprintf(stderr, "Max rules limit reached (%d), skipping rest\n", MAX_RULES);
            break;
        }

        rules[rules_count].address = strtoull(addr_str, NULL, 0);
        rules[rules_count].func = cb;

        if (n == 3) {
            strncpy(rules[rules_count].args, args, sizeof(rules[rules_count].args) - 1);
            rules[rules_count].args[sizeof(rules[rules_count].args) - 1] = '\0';
        } else {
            rules[rules_count].args[0] = '\0';
        }

        rules_count++;
    }

    fclose(f);
}
#if 0
static void print_rules(void) {
    printf("Parsed %zu rules:\n", rules_count);
    for (size_t i = 0; i < rules_count; i++) {
        printf("Rule %zu: Addr=0x%llx, Func=%p, Args='%s'\n",
               i, rules[i].address, (void *)rules[i].func, rules[i].args);
    }
}
#endif

static int core_parse_arguments(int argc, char ** argv) {
	const char *filename= utils_get_arg("detour", argc, argv);
    if (filename) {
            num_tuples = read_tuples_from_file(filename, address_tuples, MAX_TUPLES);
    }

    filename= utils_get_arg("modifier", argc, argv);
    if (filename) {
        load_update_entries(filename);
    }

    filename = utils_get_arg("virtual", argc, argv);
    if (filename) {
        parse_rules_file(filename);
    }


    filename = utils_get_arg("logger", argc, argv);
    if (filename) {
    load_logger_config(filename);
    }

    filename = utils_get_arg("monitor", argc, argv);
    if (filename) {
        runtime = filename; // Lazy Init
    }

	return 0;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
	qemu_plugin_vmstate();
	if (utils_init(argc, argv) != 0) {
			utils_die("Utils initialization failed");
	}
	if (core_parse_arguments(argc, argv) != 0) {
			utils_die("Core initialization failed");
	}

	if (vm_init(argc, argv) != 0) {
			utils_die("VM Init Failed");
	}
	if (dev_init(argc, argv) != 0) {
			utils_die("Device Init Failed");
	}

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
