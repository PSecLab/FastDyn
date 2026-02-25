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

typedef struct {
    char* name;
    uint32_t address;
} GlobalSymbol;

// Global Dictionary
GlobalSymbol* g_symbols = NULL;
size_t g_num_symbols = 0;

/* ========================================================================= */
/* The Runtime Schema Loader                                              */
/* ========================================================================= */
bool load_fastdyn_schemas(const char* filepath) {
    FILE* file = fopen(filepath, "r");
    if (!file) return false;

    g_schemas = malloc(sizeof(StructSchema) * 64);
    g_symbols = malloc(sizeof(GlobalSymbol) * 64); // Allocate symbol dictionary
    g_num_schemas = 0;
    g_num_symbols = 0;

    char marker[32];
    
    // Read the first word of every line (STRUCT or SYMBOL)
    while (fscanf(file, "%31s", marker) == 1) {
        
        if (strcmp(marker, "STRUCT") == 0) {
            char struct_name[128];
            int num_fields;
            fscanf(file, "%127s %d", struct_name, &num_fields);
            
            StructSchema* current = &g_schemas[g_num_schemas++];
            current->struct_name = strdup(struct_name);
            current->field_count = num_fields;
            current->fields = malloc(sizeof(FieldDef) * num_fields);

            for (int i = 0; i < num_fields; i++) {
                char fname[256];
                int off, sz, typ;
                fscanf(file, "%255s %d %d %d", fname, &off, &sz, &typ);
                current->fields[i].name = strdup(fname);
                current->fields[i].offset = (uint32_t)off;
                current->fields[i].size = (uint32_t)sz;
                current->fields[i].type = (FieldType)typ;
            }
        } 
        else if (strcmp(marker, "SYMBOL") == 0) {
            char sym_name[128];
            uint32_t sym_addr;
            // Parse the hex address
            fscanf(file, "%127s %x", sym_name, &sym_addr);
            
            g_symbols[g_num_symbols].name = strdup(sym_name);
            g_symbols[g_num_symbols].address = sym_addr;
            g_num_symbols++;
        }
    }
    fclose(file);
    return true;
}

uint32_t inspct_get_symbol(const char* symbol_name) {
    for (size_t i = 0; i < g_num_symbols; i++) {
        if (strcmp(g_symbols[i].name, symbol_name) == 0) {
            return g_symbols[i].address;
        }
    }
    return 0; // Symbol not found
}

uint32_t inspct_get_field_offset(const char* struct_name, const char* field_name) {
    if (!struct_name || !field_name) return 0;

    for (size_t i = 0; i < g_num_schemas; i++) {
        if (strcmp(g_schemas[i].struct_name, struct_name) == 0) {
            for (size_t j = 0; j < g_schemas[i].field_count; j++) {
                if (strcmp(g_schemas[i].fields[j].name, field_name) == 0) {
                    return g_schemas[i].fields[j].offset;
                }
            }
            return 0; /* struct found but field not found */
        }
    }
    return 0; /* struct not found */
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
