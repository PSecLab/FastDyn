#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bfd.h>

static struct bfd_hash_table table;

struct sym_entry {
    struct bfd_hash_entry root;
    unsigned long address;
};

static struct bfd_hash_entry *
map_table_entry(struct bfd_hash_entry *entry,
            struct bfd_hash_table *table,
            const char *string)
{
    struct sym_entry *ret = (struct sym_entry *) entry;
    if (!ret) {
        ret = (struct sym_entry *) bfd_hash_allocate(table,
                                                     sizeof(struct sym_entry));
    }
    if (ret) {
        ret->root.string = string;
        ret->address = 0;
    }
    return &ret->root;
}

void get_symbol_address(const char *symbol_name, unsigned long *address) {
    struct sym_entry *e = (struct sym_entry *)
        bfd_hash_lookup(&table, symbol_name, FALSE, FALSE);
    if (e) {
        *address = e->address;
    } else {
        *address = 0;
    }
}

int populate_symbol_table(const char *map_filename) {
    if (!bfd_hash_table_init(&table, map_table_entry, sizeof(struct sym_entry))) {
        fprintf(stderr, "Failed to init hash table\n");
        return 0;
    }

    FILE *fp = fopen(map_filename, "r");
    if (!fp) {
        perror("fopen");
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        const char *symbol = strdup(line);
        unsigned long addr = strtoul(colon + 1, NULL, 16);

        struct sym_entry *e = (struct sym_entry *)
            bfd_hash_lookup(&table, symbol, TRUE, TRUE);
        if (e) {
            e->address = addr;
        }
        free((void *)symbol);
    }
    fclose(fp);
    return 1;
}

void cleanup_bfd(void) {
    bfd_hash_table_free(&table);
}

// int main(int argc, char **argv) {
//     if (argc != 2) {
//         fprintf(stderr, "Usage: %s symbols.txt\n", argv[0]);
//         return 1;
//     }

//     const char *filename = argv[1];

//     if (!populate_symbol_table_from_map_file(filename)) {
//         fprintf(stderr, "Failed to populate symbol table from %s\n", filename);
//         return 1;
//     }

//     // test lookups
//     const char *test = "foo";
//     // struct sym_entry *found = (struct sym_entry *)
//     //     bfd_hash_lookup(&table, test, FALSE, FALSE);
//     unsigned long addr = 0;
//     get_symbol_address_from_mapping(test, &addr);
//     if (addr) {
//         printf("Symbol %s → 0x%lx\n", test, addr);
//     } else {
//         printf("Symbol %s not found\n", test);
//     }

//     // test get symbol address function
//     addr = 0;
//     get_symbol_address_from_mapping("bar", &addr);
//     if (addr) {
//         printf("get_symbol_address: bar → 0x%lx\n", addr);
//     } else {
//         printf("get_symbol_address: bar not found\n");
//     }

//     map_table_cleanup();
//     return 0;
// }