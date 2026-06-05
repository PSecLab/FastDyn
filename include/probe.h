#ifndef PROBE_H
#define PROBE_H

#include <stdint.h>
#include <qemu/qemu-plugin.h>

extern int probe_run;

/**
 * @brief Initialize the probe module with the given JSON file containing fault addresses.
 * @param faults_json_path Path to the probe_faults.json file.
 * @param out_dir Path to the output directory (where to save probe_result.json).
 */
void probe_init(const char *faults_json_path, const char *out_dir);

/**
 * @brief Monitor ALL MMIO reads for polling loops.
 * @param pc The program counter executing the read.
 * @param addr The MMIO address being read.
 */
void probe_check_read(uint64_t pc, uint64_t addr);

/**
 * @brief Handle a completely unhandled MMIO access by exiting immediately.
 * @param pc The program counter executing the access.
 * @param addr The MMIO address.
 * @param is_write 1 if write, 0 if read.
 */
void probe_check_unhandled_access(uint64_t pc, uint64_t addr, int is_write);

/**
 * @brief Register probe TB translation hooks with QEMU.
 * @param id The plugin ID.
 */
void probe_register_hooks(qemu_plugin_id_t id);

#endif // PROBE_H
