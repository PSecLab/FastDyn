#ifndef CORE_H
#define CORE_H

#include <stdint.h>

/**
 * @brief Get the current program counter (PC) value.
 *
 * @return uint64_t The current PC.
 */
uint64_t core_get_pc(void);

#endif /* CORE_H */

