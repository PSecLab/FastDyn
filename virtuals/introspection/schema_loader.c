#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <virtuals.h>

/* ========================================================================= */
/* 2. Schema Data Structures                                                 */
/* ========================================================================= */
typedef enum {
    FIELD_UINT32 = 0,
    FIELD_UINT16 = 1,
    FIELD_UINT8  = 2,
    FIELD_POINTER = 3,
    FIELD_STRING_INLINE = 4,
    FIELD_STRING_PTR = 5
} FieldType;

typedef struct {
    char* name;
    uint32_t offset;
    uint32_t size;
    FieldType type;
} FieldDef;

typedef struct {
    char* struct_name;
    FieldDef* fields;
    size_t field_count;
} StructSchema;

/* Global Registry for Loaded Schemas */
StructSchema* g_schemas = NULL;
size_t g_num_schemas = 0;

/* ========================================================================= */
/* The Runtime Schema Loader                                              */
/* ========================================================================= */
bool load_fastdyn_schemas(const char* filepath) {
    FILE* file = fopen(filepath, "r");
    if (!file) {
        fprintf(stderr, "[FastDyn] ERROR: Cannot open schema file: %s\n", filepath);
        return false;
    }

    g_schemas = malloc(sizeof(StructSchema) * 64);
    g_num_schemas = 0;

    char struct_name[128];
    int num_fields;

    while (fscanf(file, "%127s %d", struct_name, &num_fields) == 2) {
        StructSchema* current_schema = &g_schemas[g_num_schemas];
        current_schema->struct_name = strdup(struct_name);
        current_schema->field_count = num_fields;
        current_schema->fields = malloc(sizeof(FieldDef) * num_fields);

        for (int i = 0; i < num_fields; i++) {
            char field_name[256];
            int offset, size, type;
            
            if (fscanf(file, "%255s %d %d %d", field_name, &offset, &size, &type) == 4) {
                current_schema->fields[i].name = strdup(field_name);
                current_schema->fields[i].offset = (uint32_t)offset;
                current_schema->fields[i].size = (uint32_t)size;
                current_schema->fields[i].type = (FieldType)type;
            } else {
                fprintf(stderr, "[FastDyn] ERROR: Malformed field in struct %s\n", struct_name);
                fclose(file);
                return false;
            }
        }
        g_num_schemas++;
    }

    fclose(file);
    fprintf(stderr, "[FastDyn] Successfully loaded %zu struct schemas.\n", g_num_schemas);
    return true;
}

bool inspct_get_field(const char* struct_name, uint32_t base_addr, const char* field_name, void* out_buffer) {
    if (!struct_name || !base_addr || !field_name || !out_buffer) return false;

    StructSchema* schema = NULL;
    for (size_t i = 0; i < g_num_schemas; i++) {
        if (strcmp(g_schemas[i].struct_name, struct_name) == 0) {
            schema = &g_schemas[i];
            break;
        }
    }
    
    if (!schema) return false;

    for (size_t i = 0; i < schema->field_count; i++) {
        if (strcmp(schema->fields[i].name, field_name) == 0) {
            FieldDef field = schema->fields[i];
            uint32_t target_addr = base_addr + field.offset;
            
            if (field.type == FIELD_STRING_PTR) {
                // Read the pointer, then read the string it points to
                uint32_t string_ptr = 0;
                qemu_plugin_read_memory(target_addr, (uint8_t*)&string_ptr, sizeof(uint32_t));
                if (string_ptr != 0) {
                    qemu_plugin_read_memory(string_ptr, (uint8_t*)out_buffer, field.size);
                } else {
                    // Null pointer, zero out the buffer
                    memset(out_buffer, 0, field.size);
                }
            } else {
                // Direct memory read for ints, pointers, and inline arrays
                qemu_plugin_read_memory(target_addr, (uint8_t*)out_buffer, field.size);
            }
            return true;
        }
    }
    
    return false;
}
