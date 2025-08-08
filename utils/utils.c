#include <utils.h>
#include <string.h>
void utils_die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

char * utils_get_arg(const char * key, int argc, char **argv) {
    int len = strlen(key);
    for (int i =0; i < argc; i ++) {
        if (strncmp(argv[i], key, len) == 0) {
            return (argv[i] + len + 1);
        }
    }

    return NULL;
}
