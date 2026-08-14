#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "fuzz.h"
#include "generic.h"
#include "utils.h"

// Example JSON configuration:
// {
//   "fields": [
//     {
//       "name": "payload",
//       "location": "r3 + 4",
//       "type": "random",
//       "size": 32
//     }
//   ]
// }

static struct Field **fields;
static size_t field_count;

/* Keep memory and grammar policy in one place for easy extension. */
#define GENERIC_DEFAULT_MEMORY_BITS 32U
#define GENERIC_MAX_MEMORY_BITS 64U
#define GENERIC_MAX_NESTING 64U

static const char *generic_schema_path;

enum FieldProperty {
    FieldName = 1U << 0,
    FieldLocation = 1U << 1,
    FieldType = 1U << 2,
    FieldSize = 1U << 3,
    FieldProperties = FieldName | FieldLocation | FieldType | FieldSize,
};

struct Parser {
    const char *cursor;
    unsigned int nesting;
};

static bool parse_expression(struct Parser *parser, struct Location *result);

static void free_field(struct Field *field)
{
    if (field == NULL) {
        return;
    }

    free(field->name);
    free(field->type);
    memset(field, 0, sizeof(*field));
}

static void free_fields(struct Field **field_list, size_t count)
{
    size_t i;

    if (field_list == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        free_field(field_list[i]);
    }
    free(field_list);
}

void generic_clear_fields(void)
{
    free_fields(fields, field_count);
    fields = NULL;
    field_count = 0;
}

struct Field **generic_get_fields(size_t *count)
{
    if (count != NULL) {
        *count = field_count;
    }
    return fields;
}

static char *read_json_file(const char *path, size_t *length)
{
    FILE *file;
    char *contents = NULL;
    long file_length;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_length = ftell(file)) < 0 ||
        (uintmax_t)file_length > (uintmax_t)(SIZE_MAX - 1U)) {
        if (errno == 0) {
            errno = EIO;
        }
        goto done;
    }

    *length = (size_t)file_length;
    contents = malloc(*length + 1U);
    if (contents == NULL) {
        errno = ENOMEM;
        goto done;
    }

    if (fseek(file, 0, SEEK_SET) != 0 ||
        fread(contents, 1U, *length, file) != *length) {
        if (errno == 0) {
            errno = EIO;
        }
        free(contents);
        contents = NULL;
        goto done;
    }
    contents[*length] = '\0';

done:
    if (fclose(file) != 0 && contents != NULL) {
        free(contents);
        contents = NULL;
    }
    return contents;
}

static char *copy_json_string(const cJSON *value)
{
    char *copy;
    size_t length;

    if (!cJSON_IsString(value) || value->valuestring == NULL) {
        return NULL;
    }

    length = strlen(value->valuestring);
    copy = malloc(length + 1U);
    if (copy == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    memcpy(copy, value->valuestring, length + 1U);
    return copy;
}

static bool parse_json_size(const cJSON *value, size_t *size)
{
    double number;
    uint64_t integer;

    if (!cJSON_IsNumber(value)) {
        return false;
    }

    number = value->valuedouble;
    if (number != number || number < 0.0 ||
        number >= (double)UINT64_MAX) {
        return false;
    }

    integer = (uint64_t)number;
    if ((double)integer != number || integer > SIZE_MAX) {
        return false;
    }

    *size = (size_t)integer;
    return true;
}

static unsigned int field_property(const char *name)
{
    if (strcmp(name, "name") == 0) {
        return FieldName;
    }
    if (strcmp(name, "location") == 0) {
        return FieldLocation;
    }
    if (strcmp(name, "type") == 0) {
        return FieldType;
    }
    if (strcmp(name, "size") == 0) {
        return FieldSize;
    }
    return 0;
}

static bool load_json_field(const cJSON *object, struct Field *field)
{
    const cJSON *value;
    unsigned int properties = 0;

    if (!cJSON_IsObject(object)) {
        return false;
    }

    cJSON_ArrayForEach(value, object) {
        unsigned int property;
        char **destination;

        if (value->string == NULL) {
            return false;
        }
        property = field_property(value->string);
        if (property == 0 || (properties & property) != 0) {
            return false;
        }

        if (property == FieldSize) {
            if (!parse_json_size(value, &field->size)) {
                return false;
            }
        } else if (property == FieldLocation) {
            if (!cJSON_IsString(value) || value->valuestring == NULL ||
                !generic_parse_expression(value->valuestring,
                                          &field->location)) {
                return false;
            }
        } else {
            if (property == FieldName) {
                destination = &field->name;
            } else {
                destination = &field->type;
            }

            *destination = copy_json_string(value);
            if (*destination == NULL) {
                return false;
            }
        }

        properties |= property;
    }

    return properties == FieldProperties;
}

bool generic_load_fields(const char *path)
{
    char *contents;
    cJSON *root = NULL;
    cJSON *json_fields;
    cJSON *value;
    struct Field **loaded = NULL;
    size_t length;
    size_t count = 0;
    size_t index = 0;
    bool ok = false;

    if (path == NULL) {
        errno = EINVAL;
        return false;
    }

    errno = 0;
    contents = read_json_file(path, &length);
    if (contents == NULL) {
        return false;
    }

    root = cJSON_ParseWithLengthOpts(contents, length + 1U, NULL, 1);
    if (!cJSON_IsObject(root) || root->child == NULL ||
        root->child->next != NULL || root->child->string == NULL ||
        strcmp(root->child->string, "fields") != 0 ||
        !cJSON_IsArray(root->child)) {
        goto done;
    }

    json_fields = root->child;
    if (cJSON_GetArraySize(json_fields) <= 0) {
        goto done;
    }
    count = (size_t)cJSON_GetArraySize(json_fields);
    if (count > SIZE_MAX / sizeof(*loaded)) {
        errno = ENOMEM;
        goto done;
    }

    loaded = calloc(count, sizeof(*loaded));
    if (loaded == NULL) {
        errno = ENOMEM;
        goto done;
    }

    cJSON_ArrayForEach(value, json_fields) {
        loaded[index] = calloc(1, sizeof(*loaded[index]));
        if (loaded[index] == NULL) {
            errno = ENOMEM;
            goto done;
        }
        if (!load_json_field(value, loaded[index])) {
            goto done;
        }
        index++;
    }

    generic_clear_fields();
    fields = loaded;
    field_count = count;
    loaded = NULL;
    count = 0;
    ok = true;

done:
    cJSON_Delete(root);
    free(contents);
    free_fields(loaded, count);
    if (!ok && errno == 0) {
        errno = EINVAL;
    }
    return ok;
}

static void skip_whitespace(struct Parser *parser)
{
    while (isspace((unsigned char)*parser->cursor)) {
        parser->cursor++;
    }
}

static bool consume(struct Parser *parser, char expected)
{
    skip_whitespace(parser);
    if (*parser->cursor != expected) {
        return false;
    }

    parser->cursor++;
    return true;
}

static bool parse_number(struct Parser *parser, uint64_t *number)
{
    char *end;
    int base = 10;

    skip_whitespace(parser);
    if (!isdigit((unsigned char)*parser->cursor)) {
        return false;
    }

    if (parser->cursor[0] == '0' &&
        (parser->cursor[1] == 'x' || parser->cursor[1] == 'X')) {
        if (!isxdigit((unsigned char)parser->cursor[2])) {
            return false;
        }
        base = 16;
    }

    errno = 0;
    *number = strtoull(parser->cursor, &end, base);
    if (errno == ERANGE || end == parser->cursor) {
        return false;
    }

    parser->cursor = end;
    return true;
}

static bool parse_register(struct Parser *parser, struct Location *result)
{
    char *end;
    unsigned long long reg;

    skip_whitespace(parser);
    if (*parser->cursor != 'r' ||
        !isdigit((unsigned char)parser->cursor[1])) {
        return false;
    }

    errno = 0;
    reg = strtoull(parser->cursor + 1, &end, 10);
    if (errno == ERANGE || reg > INT_MAX) {
        return false;
    }

    parser->cursor = end;

    /*
     * Registers are value expressions: resolve them while loading the
     * schema, just like a numeric literal.  This makes a location such as
     * "r2" refer to memory at the address held in r2, while still allowing
     * arithmetic and nested dereferences to use the same expression grammar.
     * Direct register destinations will use a separate explicit syntax.
     */
    result->type = Memory;
    result->val.address = fuzz_get_register((int)reg);
    return true;
}

static bool parse_register_location(struct Parser *parser,
                                    struct Location *result)
{
    uint64_t reg;

    skip_whitespace(parser);
    if (strncmp(parser->cursor, "reg", 3) != 0) {
        return false;
    }
    parser->cursor += 3;

    if (!consume(parser, '(') || !parse_number(parser, &reg) ||
        reg > INT_MAX || !consume(parser, ')')) {
        return false;
    }

    result->type = Register;
    result->val.reg = (int)reg;
    return true;
}

static bool location_address(const struct Location *location, uint64_t *address)
{
    if (location->type == Register) {
        *address = fuzz_get_register(location->val.reg);
        return true;
    }

    if (location->type == Memory) {
        *address = location->val.address;
        return true;
    }

    return false;
}

static bool read_unsigned_memory(uint64_t address, uint64_t bits,
                                 uint64_t *number)
{
    uint8_t bytes[GENERIC_MAX_MEMORY_BITS / 8] = {0};
    size_t byte_count;
    size_t i;

    if (bits == 0 || bits > GENERIC_MAX_MEMORY_BITS) {
        return false;
    }

    byte_count = (size_t)((bits + 7U) / 8U);
    if (fuzz_read_memory(address, bytes, (int)byte_count) != 0) {
        return false;
    }

    *number = 0;
    for (i = 0; i < byte_count; i++) {
        *number |= (uint64_t)bytes[i] << (i * 8U);
    }

    if (bits < GENERIC_MAX_MEMORY_BITS) {
        *number &= UINT64_MAX >> (GENERIC_MAX_MEMORY_BITS - bits);
    }

    return true;
}

static bool dereference(struct Location address, uint64_t bits,
                        struct Location *result)
{
    uint64_t numeric_address;
    uint64_t value;

    if (!location_address(&address, &numeric_address) ||
        !read_unsigned_memory(numeric_address, bits, &value)) {
        return false;
    }

    result->type = Memory;
    result->val.address = value;
    return true;
}

static bool parse_nested_expression(struct Parser *parser,
                                    struct Location *result)
{
    bool ok;

    if (parser->nesting == GENERIC_MAX_NESTING) {
        return false;
    }

    parser->nesting++;
    ok = parse_expression(parser, result);
    parser->nesting--;
    return ok;
}

static bool parse_primary(struct Parser *parser, struct Location *result)
{
    struct Location address;
    uint64_t number;

    skip_whitespace(parser);
    if (*parser->cursor == 'r') {
        return parse_register(parser, result);
    }

    if (*parser->cursor == 'u') {
        parser->cursor++;
        if (!parse_number(parser, &number) || !consume(parser, '[') ||
            !parse_nested_expression(parser, &address) ||
            !consume(parser, ']')) {
            return false;
        }
        return dereference(address, number, result);
    }

    if (isdigit((unsigned char)*parser->cursor)) {
        if (!parse_number(parser, &number)) {
            return false;
        }
        result->type = Memory;
        result->val.address = number;
        return true;
    }

    if (consume(parser, '[')) {
        if (!parse_nested_expression(parser, &address) ||
            !consume(parser, ']')) {
            return false;
        }
        return dereference(address, GENERIC_DEFAULT_MEMORY_BITS, result);
    }

    if (consume(parser, '(')) {
        if (!parse_nested_expression(parser, result) || !consume(parser, ')')) {
            return false;
        }
        return true;
    }

    return false;
}

static bool parse_term(struct Parser *parser, struct Location *result)
{
    struct Location rhs;
    uint64_t left;
    uint64_t right;
    char op;

    if (!parse_primary(parser, result)) {
        return false;
    }

    for (;;) {
        skip_whitespace(parser);
        op = *parser->cursor;
        if (op != '*' && op != '/') {
            return true;
        }
        parser->cursor++;

        if (!parse_primary(parser, &rhs) || !location_address(result, &left) ||
            !location_address(&rhs, &right)) {
            return false;
        }

        if (op == '/' && right == 0) {
            return false;
        }

        result->type = Memory;
        result->val.address = op == '*' ? left * right : left / right;
    }
}

static bool parse_expression(struct Parser *parser, struct Location *result)
{
    struct Location rhs;
    uint64_t left;
    uint64_t right;
    char op;

    if (!parse_term(parser, result)) {
        return false;
    }

    for (;;) {
        skip_whitespace(parser);
        op = *parser->cursor;
        if (op != '+' && op != '-') {
            return true;
        }
        parser->cursor++;

        if (!parse_term(parser, &rhs) || !location_address(result, &left) ||
            !location_address(&rhs, &right)) {
            return false;
        }

        result->type = Memory;
        result->val.address = op == '+' ? left + right : left - right;
    }
}

bool generic_parse_expression(const char *expression, struct Location *result)
{
    struct Parser parser;

    if (expression == NULL || result == NULL) {
        return false;
    }

    parser.cursor = expression;
    parser.nesting = 0;
    skip_whitespace(&parser);
    if (strncmp(parser.cursor, "reg", 3) == 0) {
        if (!parse_register_location(&parser, result)) {
            return false;
        }
    } else if (!parse_expression(&parser, result)) {
        return false;
    }

    skip_whitespace(&parser);
    return *parser.cursor == '\0';
}

void generic_configure(int argc, char **argv)
{
    generic_schema_path = utils_get_arg("fuzzing_schema", argc, argv);
}

static bool generic_init(void)
{
    if (generic_schema_path == NULL || generic_schema_path[0] == '\0') {
        return false;
    }

    return generic_load_fields(generic_schema_path);
}

static void generic_get_data(void *buf, size_t size)
{
    static void *data = NULL;
    static size_t data_len = 0;
    static size_t data_ptr = 0;

    if (buf == NULL) {
        if (data == NULL) return;

        free(data);
        data = NULL;
        data_len = 0;
        data_ptr = 0;
        return;
    }
    if (data == NULL) {
        for (size_t i = 0; i < field_count; i++) {
            data_len = data_len + fields[i]->size;
        }

        data = malloc(data_len);
        if (data == NULL) {
            utils_die("Malloc returned NULL in generic.c");
        }
        memset(data, 0, data_len);

        fuzz_get_data(data, data_len);
    }

    if (data_ptr + size > data_len) {
        utils_die("generic.c data helper ran out of data, shouldn't happen");
    }

    memcpy(buf, (void*)((uint8_t*)data + data_ptr), size);
    data_ptr = data_ptr + size; // make sure this doesn't overflow later
}

void generic_callback(void)
{
    static bool init = false;

    if (!init) {
        bool success = generic_init();
        if (!success) {
            utils_die("[generic] fuzzing_schema is missing or could not be loaded");
        }

        init = true;
    }

    for (size_t i = 0; i < field_count; i++) {
        struct Field *field = fields[i];

        size_t size = field->size < 4 ? 4 : field->size;
        uint8_t *buf = malloc(size);
        if (buf == NULL) {
            utils_die("Null malloc in generic.c");
        }
        memset(buf, 0, size);

        if (!strcmp(field->type, "random")) {
            generic_get_data(buf, field->size);
        } else {
            printf("Unsopported schema type %s\n", field->type);
        }

        if (field->location.type == Register) {
            fuzz_set_register(*(uint32_t*)buf, field->location.val.reg);
        } else if (field->location.type == Memory) {
            fuzz_write_memory(field->location.val.address, buf, field->size);
        } else {
            printf("Schema location didn't parse correctly\n");
        }

        free(buf);
    }

    generic_get_data(NULL, 0);
}
