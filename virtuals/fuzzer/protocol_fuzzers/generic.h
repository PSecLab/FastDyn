#ifndef GENERIC_FUZZER_EXPRESSION_H
#define GENERIC_FUZZER_EXPRESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A parsed expression currently resolves to a memory address.  A plain
 * register is a value expression and is replaced with its current value
 * while the schema is loaded; therefore "r2" denotes the memory address
 * held in r2.  Direct register destinations will use separate syntax.
 */
struct Location {
    enum Type {
        Register,
        Memory,
    } type;

    union {
        int reg;
        uint64_t address;
    } val;
};

struct Field {
    char *name;
    struct Location location;
    char *type;
    size_t size;
};

/*
 * Load a JSON field description with a top-level "fields" array.  Every array
 * element must contain exactly one name, location, type, and size property.
 * The textual location is parsed and evaluated into Field.location while the
 * file is loaded.
 * The new configuration replaces the previous one only after the whole file
 * has been parsed successfully.  Returns false on I/O, allocation, syntax, or
 * schema errors.
 */
bool generic_load_fields(const char *path);

/* Release the currently loaded field description. */
void generic_clear_fields(void);

/*
 * Return the loaded field array.  The returned pointer remains valid until
 * generic_load_fields() succeeds again or generic_clear_fields() is called.
 */
struct Field **generic_get_fields(size_t *count);

/*
 * Parse and evaluate an expression.
 *
 *   location := "reg(" NUMBER ")" | expr
 *   expr     := term (("+" | "-") term)*
 *   term     := primary (("*" | "/") primary)*
 *   primary  := REGISTER | NUMBER | "[" expr "]"
 *             | "u" NUMBER "[" expr "]" | "(" expr ")"
 *
 * REGISTER is r followed by a decimal register number.  `reg(N)` denotes the
 * register destination N and is valid only as the whole location.  NUMBER is
 * decimal or hexadecimal (0x prefix).  Bare [expr] reads a 32-bit unsigned
 * value; uN[expr] reads an N-bit unsigned value for 1 <= N <= 64.  Values are
 * read in target little-endian byte order and widths that are not byte-aligned
 * are masked to N bits.
 *
 * Returns false for malformed input, an unsupported width, division by zero,
 * a failed memory read, or excessive nesting.  On success, result is either
 * Register for `reg(N)` or Memory with its address member containing the
 * evaluated value.
 */
bool generic_parse_expression(const char *expression, struct Location *result);

#endif
