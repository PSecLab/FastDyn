#ifndef BFD_HASH_TABLE_H
#define BFD_HASH_TABLE_H

#include <bfd.h>
#include <stddef.h>

/**
 * @brief Populate the symbol table from the given ELF file.
 *
 * This function reads the symbol table from the specified ELF file
 * and populates a hash table mapping symbol names to their addresses.
 *
 * @param elf_name The path to the ELF file.
 * @return int 0 on success, -1 on failure.
 */
int populate_symbol_table(const char *elf_name);

/**
 * @brief Clean up BFD resources.
 *
 * This function releases any resources allocated by BFD during
 * the symbol table population process.
 */
void cleanup_bfd(void);

/**
 * @brief Get the address of a symbol by name.
 *
 * This function looks up the address of a symbol in the populated
 * symbol table.
 *
 * @param symbol_name The name of the symbol to look up.
 * @param[out] address Pointer to store the found address.
 * @return void
 */
void get_symbol_address(const char *symbol_name, unsigned long *address);

#endif // BFD_HASH_TABLE_H
