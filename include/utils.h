#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>

//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
  #define DEBUG_LOG(fmt, ...) printf("DEBUG: " fmt, ##__VA_ARGS__)
#else
  #define DEBUG_LOG(fmt, ...) // nothing
#endif

// Print error message and exit
void utils_die(const char *msg);

// Get the value of a command-line argument: --key=value
// Returns NULL if not found or malformed
char *utils_get_arg(const char *key, int argc, char **argv);

#endif // UTILS_H
