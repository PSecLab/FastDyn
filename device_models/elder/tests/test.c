#include "device_config.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <device_config.ini>\n", argv[0]);
        return 1;
    }

    device_config_t devices[MAX_DEVICES] = {0};
    int n = parse_config(argv[1], devices, MAX_DEVICES);

    if (n < 0) {
        fprintf(stderr, "Failed to parse %s\n", argv[1]);
        return 1;
    }

    for (int i = 0; i < n; i++) {
        printf("[%s]\n", devices[i].section);
        printf("  libpath = %s\n", devices[i].libpath);
        printf("  base = 0x%llx\n", (unsigned long long)devices[i].base);
        printf("  size = 0x%llx\n", (unsigned long long)devices[i].size);
    }

    return 0;
}

