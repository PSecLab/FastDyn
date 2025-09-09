#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bfd.h>
#include "bfd_hash_table.h"

static bfd *abfd = NULL;
static asymbol **syms = NULL;

int populate_symbol_table(const char *filename) {
    long symcount;
    long storage_needed;

    // Initialize BFD
    bfd_init();

    // Open the file
    abfd = bfd_openr(filename, NULL);
    if (!abfd) {
        fprintf(stderr, "Error opening %s: %s\n", filename, bfd_errmsg(bfd_get_error()));
        return -1;
    }

    // Check format
    if (!bfd_check_format(abfd, bfd_object)) {
        fprintf(stderr, "File %s is not an object file\n", filename);
        bfd_close(abfd);
        abfd = NULL;
        return -1;
    }

    // Get symbol table size
    storage_needed = bfd_get_symtab_upper_bound(abfd);
    if (storage_needed <= 0) {
        fprintf(stderr, "No symbols found or error.\n");
        bfd_close(abfd);
        abfd = NULL;
        return -1;
    }

    // Allocate memory for symbols
    syms = (asymbol **)malloc(storage_needed);
    if (!syms) {
        fprintf(stderr, "Memory allocation failed.\n");
        bfd_close(abfd);
        abfd = NULL;
        return -1;
    }

    // Read the symbols
    symcount = bfd_canonicalize_symtab(abfd, syms);
    if (symcount < 0) {
        fprintf(stderr, "Error reading symbol table: %s\n", bfd_errmsg(bfd_get_error()));
        free(syms);
        bfd_close(abfd);
        syms = NULL;
        abfd = NULL;
        return -1;
    }
    return 0;
}

void cleanup_bfd() {
    if (syms) {
        free(syms);
        syms = NULL;
    }
    if (abfd) {
        bfd_close(abfd);
        abfd = NULL;
    }
}

void get_symbol_address(const char *symbol_name, unsigned long *address) {
    if (!syms) {
        fprintf(stderr, "Symbol table not populated. Call populate_symbol_table first.\n");
        *address = 0;
        return;
    }

    for (int i = 0; syms[i] != NULL; i++) {
        asymbol *sym = syms[i];
        if (strcmp(sym->name, symbol_name) == 0) {
            *address = (unsigned long)bfd_asymbol_value(sym);
            return;
        }
    }

    fprintf(stderr, "Symbol %s not found.\n", symbol_name);
    *address = 0;
}

