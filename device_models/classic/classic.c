#include <device.h>
#include <utils.h>
#include <pthread.h>
#include <unistd.h>

typedef struct {
    unsigned long start;
    unsigned long end;
} Range;

static uint64_t classic_read(void *opaque, hwaddr address, unsigned size) {
    (void)opaque;
    return 0;
}

static void classic_write(void *opaque, hwaddr address, uint64_t value, unsigned size) {
    (void)opaque;
}
static int classic_serve(int line) {
	printf("Served Line: %d \n", line);
	return 0;
}

static int classic_interrupt(int line) {
		(void)line;
		printf("Interrupt Line: %d \n", line);
		return 0;
}

int classic_parse_ranges(const char *s, Range *ranges, int max_ranges) {
    int count = 0;
    char buffer[256];
    strncpy(buffer, s, sizeof(buffer));
    buffer[sizeof(buffer)-1] = '\0';

    char *token = strtok(buffer, ",");
    while (token && count < max_ranges) {
        // Remove leading spaces
        while (*token == ' ') token++;

        char *dash = strchr(token, '-');
        if (dash) {
            *dash = '\0';
            ranges[count].start = strtoul(token, NULL, 0); // auto-detect hex with 0x
            ranges[count].end   = strtoul(dash + 1, NULL, 0);
            count++;
        }

        token = strtok(NULL, ",");
    }

    return count;
}
static int classic_init(char *argument);
// The public definition of the classic device model
DeviceModel classic_model_def = {
    .name = "classic",
    .read = classic_read,
    .write = classic_write,
    .init = classic_init,
    .serve = classic_serve,
    .interrupt = classic_interrupt,
};

static int classic_init(char *argument) {
	Range ranges[10];
	int n = classic_parse_ranges(argument, ranges, 10);
	for (int i = 0; i < n; i++) {
        dev_register_device_model(ranges[i].start, ranges[i].end, &classic_model_def);
    }
    return 0;
}

