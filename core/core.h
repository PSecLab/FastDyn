#ifndef CORE_H
#define CORE_H

#include <qemu-plugin.h>
#include "common.h"

// Function declarations
bool find_rule_by_address(unsigned long long addr, rule_t **out_rule);

#endif // CORE_H
