/**
 * @file core.c
 * @brief Core module for the dynamic analysis QEMU plugin.
 *
 * This file implements the core functionality of a dynamic analysis
 * plugin for QEMU. It is responsible for initializing the plugin,
 * handling CPU and memory events, and providing hooks for analysis
 * routines during emulation.
 *
 * The core module sets up the plugin environment, registers callbacks,
 * and maintains the internal state required to track and analyze
 * the execution of guest code.
 *
 * @note This module is tightly coupled with QEMU's plugin API and
 *       should be used only within the context of QEMU dynamic analysis.
 *
 * @author Arslan Khan
 * @date 2025-09-06
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

#include <qemu/qemu-plugin.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "core.h"
#include "common.h"
#include <utils.h>
#include <device.h>
#include <config.h>
#if ENABLE_LIBPY
#include <python.h>
#endif

#include <virtuals.h>  // For lookup_callback function

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>
#include <signal.h>

static _Atomic uint64_t g_icount = 0;

static const char * runtime;

AddressList addressLists[MAX_LISTS];
size_t listCount = 0;
int coverage;

int optifuzz = 0;

twintrace_mode_t twintrace_mode = TT_OFF;
const char *twintrace_bin_path = NULL;

#define MAX_VCPUS 8
#define DEFAULT_BBL_DUMP_PATH "bbl.txt"
#define DEFAULT_BBL_COVERAGE_PATH "fastdyn_work/bbl_coverage.txt"

static int bbl_enable = 0;
static const char *bbl_dump_path = DEFAULT_BBL_DUMP_PATH;

// per-vCPU unique set of TB entry PCs
static GHashTable *bbl_sets[MAX_VCPUS];

// stores the size of a basic block for coverage info
static GHashTable *bbl_length;

// optional: total TB executions (not unique)
static uint64_t bbl_total_tb_exec[MAX_VCPUS];

/**
 * @brief Parses a token string into a logger entry.
 *
 * This function takes a string of the form "address:reg" and parses
 * it into a `LoggerEntry` structure. The address is interpreted as
 * an unsigned integer (hex or decimal), and the register number is
 * parsed as a simple integer.
 *
 * @param token The input string to parse, expected in "address:reg" format.
 * @param entry Pointer to a `LoggerEntry` structure where the parsed
 *              address and register will be stored.
 *
 * @return `true` if parsing was successful, `false` if the input
 *         string does not contain a colon or is otherwise invalid.
 *
 * @note The function modifies the input string by inserting a null
 *       terminator at the colon position.
 */
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


QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
int counter;

#define HOOK_POINT	(0x106d6)
#define ANCHOR		(0x106cc)


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

uint64_t core_get_icount(void) {
    return atomic_load_explicit(&g_icount, memory_order_relaxed);
}

// Called from your QEMU/plugin TB callback:
void core_icount_add(uint32_t n_insns) {
    atomic_fetch_add_explicit(&g_icount, (uint64_t)n_insns, memory_order_relaxed);
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

#define LOG_BUFFER_SIZE (UINT16_MAX + 1)

// cb_registry_len moved to virtuals.c



#include <stdio.h>
#include <stdlib.h>





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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define MAX_DEV_NAME 64

typedef enum {
    ACCESS_READ,
    ACCESS_WRITE
} access_type_t;

typedef struct {
    uint64_t        pc;
    access_type_t  type;
    char            dev[MAX_DEV_NAME];
    uint64_t        offset;
	uint64_t        reg;
} access_t;

#if 0
static const char *rw_str(access_type_t t)
{
    return (t == ACCESS_READ) ? "R" : "W";
}
#endif

static size_t count;
static access_t *accesses;
static int parse_access_type(const char *s, access_type_t *out)
{
    if (!s || !*s)
        return -1;

    if (strcasecmp(s, "R") == 0 || strcasecmp(s, "READ") == 0) {
        *out = ACCESS_READ;
        return 0;
    }

    if (strcasecmp(s, "W") == 0 || strcasecmp(s, "WRITE") == 0) {
        *out = ACCESS_WRITE;
        return 0;
    }

    return -1;
}

static int parse_line(const char *line, access_t *out)
{
    char dev[MAX_DEV_NAME];
    char rw[16];
    unsigned long long pc, off, reg;

    /* Skip comments / empty lines */
    if (line[0] == '#' || line[0] == '\n')
        return 0;

    /*
     * Accepted formats:
     *   0x800123 R gpioa 0x1
     *   0x800123 READ gpioa 0x1
     */
    int n = sscanf(line, "%llx %15s %63s %llx %llx",
                   &pc, rw, dev, &off, &reg);
    if (n != 5)
        return -1;

    access_type_t type;
    if (parse_access_type(rw, &type) != 0)
        return -1;

    out->pc     = pc;
    out->type   = type;
    out->offset = off;
	out->reg    = reg;
    strncpy(out->dev, dev, MAX_DEV_NAME);
    out->dev[MAX_DEV_NAME - 1] = '\0';

    return 1;
}

static int arg_is_disabled(const char *s) {
    if (!s || !*s) return 1;
    return (strcasecmp(s, "none") == 0 ||
            strcasecmp(s, "null") == 0 ||
            strcasecmp(s, "false") == 0 ||
            strcmp(s, "0") == 0);
}

access_t *parse_access_file(const char *path, size_t *count)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen");
        return NULL;
    }

    size_t cap = 128, n = 0;
    access_t *list = malloc(cap * sizeof(*list));
    if (!list) {
        fclose(f);
        return NULL;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        access_t a;
        int r = parse_line(line, &a);
        if (r <= 0)
            continue;

        if (n == cap) {
            cap *= 2;
            access_t *tmp = realloc(list, cap * sizeof(*list));
            if (!tmp) {
                free(list);
                fclose(f);
                return NULL;
            }
            list = tmp;
        }

        list[n++] = a;
    }

    fclose(f);
    *count = n;
    return list;
}

static const access_t *find_access_by_pc(const access_t *list,
                                  size_t count,
                                  uint64_t pc)
{
    for (size_t i = 0; i < count; i++) {
        if (list[i].pc == pc)
            return &list[i];
    }
    return NULL;
}



static int init = 0;
AddressList cc_list;
LoggerEntry cc_entry;
LookupResult cc_ret;
static int tracer_ready =0;
char gpio_memory[0x400];

static void tb_exec_cb(unsigned int cpu_index, void *udata)
{
    (void)cpu_index;
    uint32_t n = (uint32_t)(uintptr_t)udata;
    core_icount_add(n);
}

static void bbl_tb_exec_cb(unsigned int vcpu_index, void *userdata)
{
    uint64_t tb_pc = (uint64_t)(uintptr_t)userdata;

    if (vcpu_index >= MAX_VCPUS) return;

    // normalize thumb bit if you want stable BB ids
    tb_pc &= ~1ULL;

    bbl_total_tb_exec[vcpu_index] += 1;
    g_hash_table_add(bbl_sets[vcpu_index], (gpointer)(uintptr_t)tb_pc);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
	if (runtime && !init) {
			qemu_plugin_load_elf((char *)runtime);
			init = 1;
	}
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;
	UpdateEntry *matches[MAX_MATCHES];

    if (twintrace_mode != TT_OFF) {
        qemu_plugin_register_vcpu_tb_exec_cb(
            tb, tb_exec_cb,
            QEMU_PLUGIN_CB_NO_REGS,
            (void *)(uintptr_t)n
        );
    }

    if (bbl_enable && n > 0) {
        uint64_t tb_pc = qemu_plugin_insn_vaddr(qemu_plugin_tb_get_insn(tb, 0));
        tb_pc &= ~1ULL;

        qemu_plugin_register_vcpu_tb_exec_cb(
            tb,
            bbl_tb_exec_cb,
            QEMU_PLUGIN_CB_NO_REGS,
            (void *)(uintptr_t)tb_pc
        );

        g_hash_table_insert(bbl_length, (gpointer)qemu_plugin_tb_vaddr(tb), (gpointer)qemu_plugin_tb_n_insns(tb));
    }

	DEBUG_LOG("->Virtual Clock: %llu \n", (unsigned long long)qemu_plugin_get_virtual_timer());

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

		//TODO: THis will bite us one day!!
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
		if (coverage) {
			if (i == 0) {
#if DEBUG
					printf("Instrumenting: 0x%lx with %ld instructions.\n", qemu_plugin_insn_vaddr(insn), n);
					for (int iter = 0; iter <n; iter++) {
							printf("	0x%lx \n", qemu_plugin_insn_vaddr(qemu_plugin_tb_get_insn(tb, iter)));
					}
#endif
					while(!tracer_ready);
					qemu_plugin_u64 entry_tmp;
					entry_tmp.offset = (size_t)&cc_ret.list->log_buf;

					//LOG PC
					qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_LOG_REG, entry_tmp, cc_ret.entry->reg);
			}
		}
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

		//2.5 Inline IO
		const access_t *a = find_access_by_pc(accesses, count, qemu_plugin_insn_vaddr(insn));
		if (a) {
			qemu_plugin_u64 entry_tmp;
			// TODO: Get Device Memory
			entry_tmp.offset = (size_t)&gpio_memory[a->offset];
			if (a->type == ACCESS_WRITE) {
		        //STORE IO or LOAD IO Based on the address
		        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_STORE_IO, entry_tmp, a->reg);
			} else {
				//STORE IO or LOAD IO Based on the address
                qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_LOAD_IO, entry_tmp, a->reg);
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
                unsigned long addr = e->target.addr;
                if (addr > 0x40000000) // This covers standard ARMv7-M / ARMv8-M memory map
                {
                    fprintf(stderr, "Requested memory access at 0x%lx does not belong to code or RAM sections\n", addr);
                    continue;
                }
                else
                {
                    qemu_plugin_u64 entry;
                    // In TCG frontend it is already set, if you want to modify it you will have to
                    // change CPSR.
                    entry.offset = (size_t)(e->value.imm);
                    qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_UPDATE_MEM, entry, e->target.addr);
                    DEBUG_LOG("Target: 0x%lx, ", e->target.addr);
                }
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

#include <stdio.h>      // for printf, fopen, fwrite, etc.
#include <stdlib.h>     // for malloc, realloc, free, exit, abort
#include <string.h>     // for memcpy
#include <stdint.h>     // for uint32_t
#include <unistd.h>     // for access, sleep, close, getopt_long
#include <fcntl.h>      // for open
#include <signal.h>     // for signal, sig_atomic_t
#include <getopt.h>     // for command-line parsing
#include <sys/mman.h>   // for mmap, munmap
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <immintrin.h>

// ---------------------------
// Configuration / defaults
// ---------------------------
#define DEFAULT_PATH "/tmp/cvg"
#define DEFAULT_DUMP_PATH "trace_log.bin"
#define BUF_SIZE (64 * 1024) //0x10000
#define WORD_SIZE sizeof(uint32_t)
#define NUM_WORDS (BUF_SIZE / WORD_SIZE)
#define INITIAL_CAPACITY 536870912 // Initial size for our dynamic array of observed values

// ---------------------------
// Globals
// ---------------------------
static uint32_t *g_observed_values = NULL; // Buffer for new values
static size_t g_observed_count = 0;        // Number of values currently buffered
static size_t g_observed_capacity = 0;     // Allocated capacity of the buffer
static uint32_t g_prev_pc = 0;

#if ENABLE_LIBFUZZ
typedef enum {
    FUZZ_EMPTY = 0, // buffer is ready for fuzzer to give an input
    FUZZ_READY = 1, // buffer is ready for anchor to read input
    FUZZ_BUSY = 2, // anchor as successfully read input
} fuzz_state_t;

typedef struct fuzz_input {
    size_t len;
    uint8_t *data;
} fuzz_input_t;

// Designed for a system where producer and consumer are each single threaded
typedef struct fuzz_buffer {
    _Atomic fuzz_state_t state;
    _Atomic uint32_t assert; // 0 = input didn't reach assert, 1 = assert, 2 = fatal assert
    fuzz_input_t *buffer;
} fuzz_buffer_t;

// since the fuzzer creates more threads, we need to use a lock in the callbacks that read/write fuzz_buffers and anchor_count
static fuzz_buffer_t **fuzz_buffers = NULL; // size is always a power of two, if anchor count reaches new power of two, double
static int anchor_count = -1;
static int buffer_capacity = 0;
static pthread_rwlock_t anchor_lock;

#define MAP_SIZE 65536 // should always match the rust definition
extern uint8_t CVG[MAP_SIZE];

// 0 on fail, 1 otherwise
int fuzz_init(uint32_t anchor_id, char *numbers);

// allocate buffer (Rust -> C)
fuzz_buffer_t *fuzz_buffer_create(uint32_t anchor_id) {
    pthread_rwlock_wrlock(&anchor_lock);
    if (anchor_count > buffer_capacity) {
        if (buffer_capacity == 0) buffer_capacity = 1;
        while (anchor_count > buffer_capacity) {
            buffer_capacity *= 2;
        }

        fuzz_buffers = realloc(fuzz_buffers, buffer_capacity * sizeof(fuzz_buffer_t*));
        if (fuzz_buffers == NULL) {
            utils_die("Failed to realloc buffer for fuzzing");
        }
    }

    fuzz_buffers[anchor_id] = malloc(sizeof(fuzz_buffer_t));
    if (fuzz_buffers[anchor_id] == NULL) {
        utils_die("Failed to allocate new fuzzing object");
    }

    atomic_init(&fuzz_buffers[anchor_id]->state, FUZZ_EMPTY);
    atomic_init(&fuzz_buffers[anchor_id]->assert, 0);
    fuzz_buffers[anchor_id]->buffer = NULL;

    fuzz_buffer_t *buffer = fuzz_buffers[anchor_id];

    pthread_rwlock_unlock(&anchor_lock);

    return buffer;
}

// input side (Rust -> C)
bool fuzz_buffer_write(fuzz_buffer_t *fb, fuzz_input_t *input) {
    fuzz_state_t state = atomic_load_explicit(&fb->state, memory_order_acquire);
    if (state == FUZZ_EMPTY) {
        fb->buffer = input;
        atomic_store_explicit(&fb->state, FUZZ_READY, memory_order_release);
        return true;
    } else {
        fprintf(stderr, "Fuzzer not empty\n");
        return false;
    }
}

// C function for retrieving input
int fuzz_buffer_read(uint32_t anchor_id, char* out, size_t len) {
    pthread_rwlock_rdlock(&anchor_lock);
    fuzz_buffer_t *fb = fuzz_buffers[anchor_id];
    pthread_rwlock_unlock(&anchor_lock);

    while (atomic_load_explicit(&fb->state, memory_order_acquire) != FUZZ_READY) {
        _mm_pause();
    }
    if (fb->buffer == NULL) {
        fprintf(stderr, "Input is ready but buffer is null\n");
        return 0;
    }

    atomic_store_explicit(&fb->state, FUZZ_BUSY, memory_order_release); // clear assert flag
    atomic_store_explicit(&fb->assert, 0, memory_order_release); // clear assert flag

    memcpy(out, fb->buffer->data, len < fb->buffer->len ? len : fb->buffer->len);
    size_t input_size = fb->buffer->len;

    free(fb->buffer->data);
    free(fb->buffer);

    fb->buffer = NULL;

    return input_size;
}

bool fuzz_check_empty(fuzz_buffer_t *fb) {
    return (atomic_load_explicit(&fb->state, memory_order_relaxed) == FUZZ_EMPTY);
}

void fuzz_start(uint32_t anchor_id) {
    while (anchor_id >= buffer_capacity) {
        _mm_pause();
    }
}

// If the anchor is busy (didn't skip consuming input), set it to done
void fuzz_finish(uint32_t anchor_id) {
    pthread_rwlock_rdlock(&anchor_lock);
    fuzz_buffer_t *fb = fuzz_buffers[anchor_id];
    pthread_rwlock_unlock(&anchor_lock);

    if (atomic_load_explicit(&fb->state, memory_order_acquire) == FUZZ_BUSY) {
        atomic_store_explicit(&fb->state, FUZZ_EMPTY, memory_order_release);
    }
}

uint32_t fuzz_check_assert(fuzz_buffer_t *fb) {
    uint32_t assrt = atomic_load_explicit(&fb->assert, memory_order_relaxed);
    return assrt;
}

void fuzz_report_assert(uint32_t anchor_id, bool fatal) {
    pthread_rwlock_rdlock(&anchor_lock);
    if (fatal) {
        atomic_store_explicit(&fuzz_buffers[anchor_id]->assert, 2, memory_order_release);
    } else {
        atomic_store_explicit(&fuzz_buffers[anchor_id]->assert, 1, memory_order_release);
    }
    pthread_rwlock_unlock(&anchor_lock);
}

void initialize_anchor(char* args) {
    if (!args) return;

    if (anchor_count == -1) {
        pthread_rwlock_init(&anchor_lock, NULL);
        anchor_count = 0;
    }

    char tmp[301] = {0};

    char *ignored_id = strtok(args, ":"); // consume the assigned id, we're just going to assign from 0 up and overwrite this
    char *numbers = strtok(NULL, ":");
    if (numbers == NULL) {
        utils_die("Failed to read anchor targets");
    }

    strncpy(tmp, numbers, sizeof(tmp) - 1);

    pthread_rwlock_wrlock(&anchor_lock);
    int anchor_id = anchor_count++;
    pthread_rwlock_unlock(&anchor_lock);

    sprintf(args, "%d", anchor_id);
    strcat(args, ":");
    strcat(args, tmp);

    if (!fuzz_init(anchor_id, numbers)) {
        utils_die("Failed initialization");
    }
}

void add_observed_value(uint32_t val) {
    //printf("Observing %lx\n", val);
    if (g_prev_pc != 0) {
        uint32_t idx = (g_prev_pc ^ val) % MAP_SIZE;
        CVG[idx] = CVG[idx] + 1;
    }

    g_prev_pc = val;
}

#else

#define MAP_SIZE 65536 // should always match the rust definition
static uint8_t CVG[MAP_SIZE];

void add_observed_value(uint32_t val) {
    if (g_prev_pc != 0) {
        uint32_t idx = (g_prev_pc ^ val) % MAP_SIZE;
        CVG[idx] = CVG[idx] + 1;
    }

    g_prev_pc = val;

    if (g_observed_count >= g_observed_capacity) {
        // Grow the buffer (double the capacity, or start with initial size)
        size_t new_capacity = (g_observed_capacity == 0) ? INITIAL_CAPACITY : g_observed_capacity * 2;
        uint32_t *new_buf = realloc(g_observed_values, new_capacity * sizeof(uint32_t));
        if (!new_buf) {
            perror("[-] Failed to reallocate memory for observed values");
            return; // Continue without adding, or could choose to exit
        }
        g_observed_values = new_buf;
        g_observed_capacity = new_capacity;
    }
    g_observed_values[g_observed_count++] = val;
}
#endif

/**
 * @brief Writes the buffered values to a binary file.
 * @param filename The path to the output file.
 */
void dump_values(const char *filename) {
    if (!filename) {
        filename = DEFAULT_DUMP_PATH;
    }
    if (g_observed_count == 0) {
        printf("[+] No new values were observed. Nothing to dump.\n");
        return;
    }

    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("[-] Failed to open dump file for writing");
        return;
    }

    size_t written = fwrite(g_observed_values, WORD_SIZE, g_observed_count, f);
    if (written != g_observed_count) {
        fprintf(stderr, "[-] Error dumping values: tried to write %zu, but only wrote %zu\n", g_observed_count, written);
    } else {
        printf("[+] Dumped %zu entries to %s\n", g_observed_count, filename);
    }

    fclose(f);
}

void serialize_coverage(const char *filename) {
    char tmp[512];

    /* Construct temp filename in same directory */
    snprintf(tmp, sizeof(tmp), "%s.tmp", filename);

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        perror("[-] Failed to open temp coverage file for writing");
        return;
    }

    // comma separated values
    for (size_t i = 0; i < MAP_SIZE; i++) {
        if (fprintf(f, "%u", CVG[i]) < 0) {
            perror("[-] Write error");
            fclose(f);
            remove(tmp);
            return;
        }
        if (i < MAP_SIZE - 1) {
            fputc(',', f);
        }
    }
    fputc('\n', f);

    /* Ensure data is flushed to disk before rename */
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    /* Atomic rename replaces old file */
    if (rename(tmp, filename) != 0) {
        perror("[-] Failed to rename temp coverage file");
        remove(tmp);
    }
}

void reset_and_dump_values(const char *filename) {
    g_observed_count = 0; // Reset count after dumpin
    dump_values(filename);
}

void* tracer(void* arg) {
	cc_list.count =1;
	cc_ret.entry = &cc_entry;
    cc_ret.list = &cc_list;
	cc_ret.entry->reg = 15;
	cc_ret.list->log_buf.buffer = malloc(UINT16_MAX + 1);
	uint16_t tracer_index =0;

	tracer_ready=1;

	//Maybe make read atomic
	while(true) {
		while((uint16_t) (cc_list.log_buf.index - tracer_index)) {
			add_observed_value(cc_list.log_buf.buffer[tracer_index/4]);
			tracer_index +=4;//32bit PC
		}
	}
}

void save_all_bbl_sets(const char *dir) {
    int total = 0;
    for (int i = 0; i < MAX_VCPUS; i++) {
        if (!bbl_sets[i])
            continue;

        char path[256];
        snprintf(path, sizeof(path), "%s/bbl_set_%d.bin", dir, i);

        FILE *f = fopen(path, "wb");
        if (!f)
            continue;

        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init(&iter, bbl_sets[i]);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            gulong pc = (gulong) key;
            fwrite(&pc, sizeof(pc), 1, f);
            total = total + 1;
        }

        fclose(f);
    }
    printf("Saved %d basic blocks\n", total);
}

void load_all_bbl_sets(const char *dir) {
    int total = 0;
    for (int i = 0; i < MAX_VCPUS; i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/bbl_set_%d.bin", dir, i);

        FILE *f = fopen(path, "rb");
        if (!f) {
            bbl_sets[i] = g_hash_table_new(g_direct_hash, g_direct_equal);
            continue;
        }

        GHashTable *table = g_hash_table_new(g_direct_hash, g_direct_equal);

        gulong pc;
        while (fread(&pc, sizeof(pc), 1, f) == 1) {
            total = total + 1;
            g_hash_table_insert(table, (gpointer) pc, NULL);
        }

        fclose(f);
        bbl_sets[i] = table;
    }
    printf("Loaded %d blocks\n", total);
}

void save_bbl_length(const char *path) {
    if (!bbl_length)
        return;

    FILE *f = fopen(path, "wb");
    if (!f)
        return;

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, bbl_length);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        gulong pc = (gulong) key;
        guint size = GPOINTER_TO_UINT(value);

        fwrite(&pc, sizeof(pc), 1, f);
        fwrite(&size, sizeof(size), 1, f);
    }

    fclose(f);
}

void load_bbl_length(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        bbl_length = g_hash_table_new(g_direct_hash, g_direct_equal);
        return;
    }

    GHashTable *table = g_hash_table_new(g_direct_hash, g_direct_equal);

    gulong pc;
    guint size;

    while (fread(&pc, sizeof(pc), 1, f) == 1 &&
           fread(&size, sizeof(size), 1, f) == 1) {
        g_hash_table_insert(table, (gpointer) pc, GUINT_TO_POINTER(size));
    }

    fclose(f);
    bbl_length = table;
}

void print_unique_bbl(void)
{
    uint64_t unique_blocks = 0;
    uint64_t total_blocks = 0;
    for (int i = 0; i < MAX_VCPUS; i++) {
        if (bbl_sets[i]) unique_blocks += (uint64_t)g_hash_table_size(bbl_sets[i]);
        total_blocks += bbl_total_tb_exec[i];
    }

    printf("Unique Blocks: %lu, Total Blocks: %lu\n", unique_blocks, total_blocks);
}

void dump_bbl(void)
{
    save_all_bbl_sets("/root/rooney/FastDyn/fuzz_out");
    save_bbl_length("/root/rooneyFastDyn/fuzz_out/bbl_length.bin");

    uint64_t unique = 0;
    for (int i = 0; i < MAX_VCPUS; i++) {
        if (bbl_sets[i]) unique += (uint64_t)g_hash_table_size(bbl_sets[i]);
    }

    FILE *f = fopen(bbl_dump_path ? bbl_dump_path : DEFAULT_BBL_DUMP_PATH, "w");
    if (!f) return;
    FILE *f_extra = fopen(DEFAULT_BBL_COVERAGE_PATH, "w");
    if (!f_extra) return;

    fprintf(f, "bbl_unique=%" PRIu64 "\n", unique);
    uint64_t total_tb = 0;
    for (int i = 0; i < MAX_VCPUS; i++) total_tb += bbl_total_tb_exec[i];
    fprintf(f, "tb_exec_total=%" PRIu64 "\n", total_tb);
    for (int i = 0; i < MAX_VCPUS; i++) {
        guint glength;
        gpointer* gptr = g_hash_table_get_keys_as_array(bbl_sets[i], &glength);
        if (gptr) {
            for (uint32_t n = 0; n < glength; n++) {
                fprintf(f_extra, "%lx\t%lu\n", (uintptr_t)gptr[n], (uintptr_t)g_hash_table_lookup(bbl_length, gptr[n]));
            }
        }
    }

    fclose(f);
    fclose(f_extra);
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
	printf("FastDyn Exit.\n");
	if (coverage && !optifuzz) {
		dump_values(DEFAULT_DUMP_PATH);
	}

    if (optifuzz) {
        serialize_coverage("/root/rooney/FastDyn/fuzzer/libafl_phi/covg.csv");
        printf("Serialized coverage to /root/rooney/FastDyn/fuzzer/libafl_phi/covg.csv\n");
    }

    if (bbl_enable) {
        dump_bbl();
    }
}

// lookup_callback function moved to virtuals.c


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

static const char* safe_arg(const char* s) {
    if (!s) return NULL;
    if (arg_is_disabled(s)) return NULL;
    if (s[0] == '\0') return NULL;
    return s;
}

static void parse_twintrace_args(int argc, char **argv)
{
    const char *tt  = safe_arg(utils_get_arg("twintrace", argc, argv));
    const char *bin = safe_arg(utils_get_arg("twintrace_binary", argc, argv));

    twintrace_mode = TT_OFF;
    twintrace_bin_path = NULL;

    if (tt) {
        if (!strcasecmp(tt, "replay")) {
            twintrace_mode = TT_REPLAY;
        } else if (!strcasecmp(tt, "record")) {
            twintrace_mode = TT_RECORD;
        } else if (!strcasecmp(tt, "true") || !strcasecmp(tt, "on") || !strcmp(tt, "1")) {
            // if user just says "true", pick a default; I'd default to REPLAY only if bin exists,
            // otherwise RECORD. Here's a reasonable default:
            twintrace_mode = bin ? TT_REPLAY : TT_RECORD;
        } else if (!strcasecmp(tt, "false") || !strcasecmp(tt, "off") || !strcmp(tt, "0")) {
            twintrace_mode = TT_OFF;
        } else {
            fprintf(stderr, "fastdyn: unknown twintrace mode: '%s'\n", tt);
            utils_die("bad twintrace mode");
        }
    }

    if (twintrace_mode == TT_REPLAY) {
        if (!bin) {
            utils_die("fastdyn: twintrace enabled but twintrace_binary is missing");
        }
        twintrace_bin_path = bin;
    }

    fprintf(stderr, "fastdyn: twintrace_mode=%d twintrace_binary=%s\n",
            (int)twintrace_mode,
            twintrace_bin_path ? twintrace_bin_path : "(none)");
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

        char addr_str[64];
        char cb_name[64];
        char args[301] = {0};

        int n = sscanf(line, "%63s %63s %300[^\n]", addr_str, cb_name, args);
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
#if ENABLE_LIBFUZZ
        if (!strcmp("anchor", cb_name)) {
            initialize_anchor(args);
        }
#endif
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
static pthread_t thread;
static int core_parse_arguments(int argc, char ** argv) {
	const char *filename= utils_get_arg("detour", argc, argv);
    if (filename) {
            num_tuples = read_tuples_from_file(filename, address_tuples, MAX_TUPLES);
    }

    filename = utils_get_arg("modifier", argc, argv);
    if (filename) {
        load_update_entries(filename);
    }

    filename = utils_get_arg("virtual", argc, argv);
    if (filename) {
        parse_rules_file(filename);
    }

	filename = utils_get_arg("finline", argc, argv);
    if (!arg_is_disabled(filename)) {
        accesses = parse_access_file(filename, &count);
    }

	//Filename should really be value
    filename = utils_get_arg("coverage", argc, argv);
    if (!arg_is_disabled(filename) &&
        (strcasecmp(filename, "true") == 0 || strcmp(filename, "1") == 0)) {

        coverage = 1;
        if (pthread_create(&thread, NULL, tracer, NULL) != 0) {
            perror("Failed to create thread");
            exit(1);
        }
    } else {
        coverage = 0;
    }

    filename = utils_get_arg("optifuzz", argc, argv);
    if (!arg_is_disabled(filename) &&
        (strcasecmp(filename, "true") == 0 || strcmp(filename, "1") == 0)) {
        optifuzz = 1;
    } else {
        optifuzz = 0;
    }

    //parse args for twintrace
    parse_twintrace_args(argc, argv);

    //basic block coverage calculator
    const char *arg = utils_get_arg("bbl", argc, argv);
    if (!arg_is_disabled(arg) && arg) {
        bbl_enable = 1;
        if (strcmp(arg, "1") != 0 && strcasecmp(arg, "true") != 0) {
            bbl_dump_path = arg; // treat as path
        }
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

    #if ENABLE_LIBHW | ENABLE_LIBDEV
	if (dev_init(argc, argv) != 0) {
			utils_die("Device Init Failed");
	}
    #endif

    if (bbl_enable) {
        for (int i = 0; i < MAX_VCPUS; i++) {
            bbl_sets[i] = g_hash_table_new(g_direct_hash, g_direct_equal);
        }
        bbl_length = g_hash_table_new(g_direct_hash, g_direct_equal);
        memset(bbl_total_tb_exec, 0, sizeof(bbl_total_tb_exec));

        load_all_bbl_sets("/root/rooney/FastDyn/fuzz_out");
        load_bbl_length("/root/rooney/FastDyn/fuzz_out/bbl_length.bin");
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    #if ENABLE_LIBPY
        if (python_vm_setup() != 0) {
                utils_die("Python VM Init Failed");
        }
    #endif


    return 0;
}
