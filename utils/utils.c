#include <utils.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// Global log handle
static FILE *fp;

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

void utils_log_to_file(FILE *fp, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

	fflush(fp);  // ensure log is flushed to disk
}

FILE *utils_log_fp(void) {
    return fp;
}

int utils_init(int argc, char** argv) {
	fp = fopen("debug.log", "a");
	if (!fp) return -1;
	return 0;
}
