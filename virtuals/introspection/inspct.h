

#ifndef INSPECT_H
#define INSPECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <virtuals.h>

bool inspct_get_field(const char* struct_name, uint32_t base_addr, const char* field_name, void* out_buffer);
uint32_t inspct_get_field_offset(const char* struct_name, const char* field_name);
uint32_t inspct_get_symbol(const char* symbol_name);
bool load_fastdyn_schemas(const char* filepath);
int inspct_init(int argc, char ** argv, const char *schema_path);
int inspct_is_enabled(void);
const char *inspct_get_mode(void);
const char *inspct_get_out_dir(void);
uint32_t inspct_get_max_events(void);
bool inspct_write_probe_json(FILE *f, const char *indent);
void inspct_write_summary_file(void);

#endif // INSPECT_H
