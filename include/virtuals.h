#ifndef VIRTUALS_H
#define VIRTUALS_H

#include <qemu-plugin.h>
#include <stdint.h>
#include "common.h"
#include "config.h"

// Virtual instruction functions
void raiseirq(unsigned int cpu_index, void *udata);
void pulseirq(unsigned int cpu_index, void *udata);
void updatepc(unsigned int cpu_index, void *udata);
void updatereg(unsigned int cpu_index, void *udata);
void updatemem(unsigned int cpu_index, void *udata);
void randstate(unsigned int cpu_index, void *udata);
void dumplogger(unsigned int cpu_index, void *udata);
void dyninst(unsigned int cpu_index, void *udata);
void dyninst_lib(unsigned int cpu_index, void *udata);
void fastdyn_callback(unsigned int cpu_index, void *udata);
void timer_start(unsigned int cpu_index, void *udata);
void start_budgeting(unsigned int cpu_index, void *udata);
void debug_log(unsigned int cpu_index, void *udata);

/**
 * @brief Callbacks to communicate with gazebo
 */
#if ENABLE_LIBGZ
void gz_service(unsigned int cpu_index, void *udata);

/**
 * @brief Wheel Encoder Copy state to Frontend function
 */
void copy_state_to_frontend(unsigned int cpu_index, void *udata);

/**
 * @brief Compass Block Read function
 */
void compass_block_read(unsigned int cpu_index, void *udata);

/**
 * @brief Send mavlink GPS to firmware
 */
void send_mavlink_gps_data(unsigned int cpu_index, void *udata);
#endif

// Helper functions
unsigned char get_random_byte(void);
uint32_t get_random_word(void);
unsigned long long* parse_addresses(const char *input, size_t *count);
int parse_update_mem_arg(const char *input, MemAccess *out);
AddrFilePair parse_addr_file(const char *input);
void* read_file(const char *filename, size_t *length);
bool parse_file_entry(const char* line, FileEntry* entry);
void dump_log_buffer_to_file(const AddressList* list, const char *filename);

// Callback lookup function (moved from core.c)
cb_func_t lookup_callback(const char *name);

// External dependencies from core.c
extern AddressList addressLists[];
extern size_t listCount;
extern uint32_t qemu_get_register(int reg);
extern void qemu_set_register(uint32_t value, int reg);

// Initialization function
int virtuals_init(int argc, char **argv);

#endif // VIRTUALS_H 
