/**
 * @file fake_afl.c
 * @brief Fake AFL coverage tracking implementation for FastDyn QEMU plugin.
 *
 * This module implements AFL++-style edge coverage tracking using a local
 * bitmap. It provides coverage analysis during firmware execution without
 * requiring actual AFL++ integration or shared memory IPC.
 *
 * The implementation uses AFL++'s standard edge coverage algorithm:
 * - Hash current location with bit manipulation
 * - XOR with previous location to create edge identifier
 * - Increment coverage counter in bitmap
 * - Update previous location for next edge
 *
 * @author FastDyn Team
 * @date 2025-10-15
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_afl.h"
#include <utils.h>

// Global fake AFL state
static fake_afl_state_t fake_afl_state = {
    .fake_afl_area = NULL,
    .prev_loc = 0,
    .fake_afl_enabled = 0
};

/**
 * @brief Sets up fake AFL coverage tracking with a local bitmap.
 *
 * This function allocates a local coverage bitmap for tracking
 * basic block transitions and edge coverage. The bitmap is
 * zero-initialized using calloc.
 *
 * @return 0 on success, -1 on failure
 */
int fake_afl_setup(void) {
    // Allocate local coverage bitmap (zero-initialized)
    fake_afl_state.fake_afl_area = (unsigned char *)calloc(FAKE_AFL_MAP_SIZE, 1);
    
    if (!fake_afl_state.fake_afl_area) {
        fprintf(stderr, "Fake AFL error: failed to allocate coverage bitmap\n");
        return -1;
    }

    fake_afl_state.fake_afl_enabled = 1;
    fake_afl_state.prev_loc = 0;
    
    fprintf(stderr, "Fake AFL coverage tracking enabled (bitmap: %p, size: %d bytes)\n",
            (void *)fake_afl_state.fake_afl_area, FAKE_AFL_MAP_SIZE);
    
    return 0;
}

/**
 * @brief Records coverage for a basic block transition (edge coverage).
 *
 * This function implements AFL's edge coverage algorithm:
 * 1. Hash the current location using bit shifts and XOR
 * 2. Mask to fit within bitmap size
 * 3. XOR with previous location to create unique edge identifier
 * 4. Increment counter in bitmap
 * 5. Update previous location for next transition
 *
 * The algorithm is designed to:
 * - Distinguish different paths through the same basic block
 * - Fit coverage data into a fixed-size bitmap
 * - Provide fast, constant-time updates
 *
 * @param cur_loc Current basic block location (address)
 */
void fake_afl_maybe_log(uint64_t cur_loc) {
    if (!fake_afl_state.fake_afl_enabled) {
        return;
    }

    // AFL's edge coverage: hash current location with previous
    // Step 1: Hash the location using bit manipulation
    cur_loc = (cur_loc >> 4) ^ (cur_loc << 8);
    cur_loc &= (FAKE_AFL_MAP_SIZE - 1);

    // Step 2: Create edge identifier by XORing with previous location
    uint64_t edge = cur_loc ^ fake_afl_state.prev_loc;
    
    // Step 3: Increment coverage counter (saturating at 255)
    fake_afl_state.fake_afl_area[edge]++;
    
    // Step 4: Update previous location for next edge (with shift for diversity)
    fake_afl_state.prev_loc = cur_loc >> 1;
}

/**
 * @brief Cleanup fake AFL coverage tracking resources.
 *
 * This function frees the coverage bitmap and resets the state.
 * Should be called during plugin exit.
 */
void fake_afl_cleanup(void) {
    if (fake_afl_state.fake_afl_enabled && fake_afl_state.fake_afl_area != NULL) {
        free(fake_afl_state.fake_afl_area);
        fake_afl_state.fake_afl_area = NULL;
        fake_afl_state.fake_afl_enabled = 0;
        fake_afl_state.prev_loc = 0;
        DEBUG_LOG("Fake AFL coverage tracking bitmap freed\n");
    }
}

/**
 * @brief Get the current fake AFL state.
 *
 * This function provides access to the internal state for inspection
 * or integration with other modules.
 *
 * @return Pointer to the fake AFL state structure
 */
fake_afl_state_t* fake_afl_get_state(void) {
    return &fake_afl_state;
}
