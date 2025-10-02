#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>

#define DEBUG_FILE


#if defined(DEBUG_PRINT)
  #define DEBUG_LOG(fmt, ...) \
    printf(fmt "\n", ##__VA_ARGS__)
#elif defined(DEBUG_FILE)
  FILE *utils_log_fp(void);
  void utils_log_to_file(FILE *fp, const char *fmt, ...);
  #define DEBUG_LOG(fmt, ...) \
    utils_log_to_file(utils_log_fp(), fmt "\n", ##__VA_ARGS__)
#else
  #define DEBUG_LOG(fmt, ...) ((void)0)
#endif

typedef struct {
    unsigned long start;
    unsigned long end;
} Range;

// Print error message and exit
void utils_die(const char *msg);

// Get the value of a command-line argument: --key=value
// Returns NULL if not found or malformed
char *utils_get_arg(const char *key, int argc, char **argv);

int utils_init(int argc, char ** argv);

void utils_parse_ranges(int range_count, char **overall_ranges, Range *ranges);
int* utils_parse_interrupt_ranges(const char *s, int *int_nums);
#endif // UTILS_H
