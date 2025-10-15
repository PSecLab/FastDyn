/**
 * @file fake_afl.h
 * @brief Fake AFL coverage tracking header for FastDyn QEMU plugin.
 *
 * This module implements AFL++-style edge coverage tracking using a local
 * bitmap. It tracks basic block transitions during firmware execution to
 * provide coverage analysis without requiring actual AFL++ integration.
 *
 * @author FastDyn Team
 * @date 2025-10-15
 */

#ifndef FAKE_AFL_H
#define FAKE_AFL_H

#include <stdint.h>

// Fake AFL coverage tracking constants
#define FAKE_AFL_MAP_SIZE 65536

/**
 * @brief Fake AFL coverage tracking state structure.
 *
 * This structure maintains the state for AFL++-style coverage tracking,
 * including the coverage bitmap and previous location for edge detection.
 */
typedef struct {
    unsigned char *fake_afl_area;     // Coverage bitmap (65KB)
    uint64_t prev_loc;                // Previous basic block location for edge tracking
    int fake_afl_enabled;             // Flag to enable/disable fake AFL mode
} fake_afl_state_t;

/**
 * @brief Sets up fake AFL coverage tracking with a local bitmap.
 *
 * This function allocates a local coverage bitmap for tracking
 * basic block transitions and edge coverage.
 *
 * @return 0 on success, -1 on failure
 */
int fake_afl_setup(void);

/**
 * @brief Records coverage for a basic block transition (edge coverage).
 *
 * This function implements AFL's edge coverage algorithm by XORing
 * the current location with the previous location and incrementing
 * the corresponding bitmap entry.
 *
 * @param cur_loc Current basic block location (address)
 */
void fake_afl_maybe_log(uint64_t cur_loc);

/**
 * @brief Cleanup fake AFL coverage tracking resources.
 *
 * This function frees the coverage bitmap and resets the state.
 */
void fake_afl_cleanup(void);

/**
 * @brief Get the current fake AFL state.
 *
 * @return Pointer to the fake AFL state structure
 */
fake_afl_state_t* fake_afl_get_state(void);

#endif // FAKE_AFL_H
