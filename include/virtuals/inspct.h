#ifndef INSPCT_H
#define INSPCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int inspct_init(int argc, char **argv, const char *schema_path);
int inspct_is_enabled(void);
const char *inspct_get_mode(void);
const char *inspct_get_out_dir(void);
uint32_t inspct_get_max_events(void);
bool inspct_write_probe_json(FILE *f, const char *indent);
void inspct_write_summary_file(void);

#ifdef __cplusplus
}
#endif

#endif /* INSPCT_H */
