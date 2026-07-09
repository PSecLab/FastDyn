/*
 * Zephyr RTOS external introspection hooks.
 *
 * z_thread_mark_switched_in/out do not pass the thread pointer as an ABI
 * argument on this firmware. Sample _kernel.cpus[0].current instead.
 */
#include <core.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inspct.h"
#include "virtuals.h"

#define ZEPHYR_THREAD_NAME_MAX 32
#define ZEPHYR_SWITCH_RING_CAP 16384
#define ZEPHYR_PROBE_RECENT_SWITCHES 8

#define ZEPHYR_FALLBACK_CPU_CURRENT_OFFSET 0x8U
#define ZEPHYR_FALLBACK_THREAD_PENDED_ON_OFFSET 0x8U
#define ZEPHYR_FALLBACK_THREAD_STATE_OFFSET 0xdU
#define ZEPHYR_FALLBACK_THREAD_PRIO_OFFSET 0xeU
#define ZEPHYR_FALLBACK_THREAD_NAME_OFFSET 0x98U

typedef struct {
    uint32_t thread_addr;
    char name[ZEPHYR_THREAD_NAME_MAX];
    int32_t priority;
    uint32_t state;
    uint32_t pended_on;
    uint64_t scheduled_count;
} ZephyrThreadState;

typedef struct {
    uint64_t seq;
    uint64_t icount;
    char direction[4];
    ZephyrThreadState thread;
} ZephyrSwitchEvent;

static ZephyrThreadState all_threads[128];
static size_t all_threads_count = 0;
static ZephyrSwitchEvent switch_ring[ZEPHYR_SWITCH_RING_CAP];
static uint64_t switch_count = 0;
static uint32_t switch_ring_count = 0;
static ZephyrThreadState current_thread;
static bool have_current_thread = false;

static bool zephyr_debug_enabled(void) {
    const char *mode = inspct_get_mode();
    return mode && strcmp(mode, "debug") == 0;
}

static uint32_t zephyr_ring_limit(void) {
    uint32_t limit = inspct_get_max_events();
    if (limit == 0) {
        limit = 1;
    }
    if (limit > ZEPHYR_SWITCH_RING_CAP) {
        limit = ZEPHYR_SWITCH_RING_CAP;
    }
    return limit;
}

static void json_write_escaped(FILE *f, const char *s) {
    fputc('"', f);
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            if (*p == '"' || *p == '\\') {
                fputc('\\', f);
                fputc(*p, f);
            } else if (*p >= 32 && *p < 127) {
                fputc(*p, f);
            } else {
                fprintf(f, "\\u%04x", *p);
            }
        }
    }
    fputc('"', f);
}

static bool zephyr_range_contains(uint32_t addr, uint32_t size,
                                  uint32_t start, uint32_t end) {
    if (size == 0 || addr < start || addr >= end) {
        return false;
    }
    uint32_t last = addr + size - 1;
    return last >= addr && last < end;
}

static bool zephyr_is_ram_addr(uint32_t addr, uint32_t size) {
    return zephyr_range_contains(addr, size, 0x20000000U, 0x20400000U) ||
           zephyr_range_contains(addr, size, 0x00000000U, 0x00100000U);
}

static bool zephyr_is_string_addr(uint32_t addr, uint32_t size) {
    return zephyr_is_ram_addr(addr, size) ||
           zephyr_range_contains(addr, size, 0x60000000U, 0x60800000U) ||
           zephyr_range_contains(addr, size, 0x70000000U, 0x70800000U);
}

static bool zephyr_read_memory(uint32_t addr, void *out, uint32_t size) {
    if (!out || size == 0 || !zephyr_is_string_addr(addr, size)) {
        return false;
    }
    return qemu_plugin_read_memory(addr, (uint8_t *)out, (int)size) == 0;
}

static bool zephyr_read_thread_memory(uint32_t addr, void *out, uint32_t size) {
    if (!out || size == 0 || !zephyr_is_ram_addr(addr, size)) {
        return false;
    }
    return qemu_plugin_read_memory(addr, (uint8_t *)out, (int)size) == 0;
}

static void zephyr_sanitize_string(char *name, size_t name_size) {
    if (!name || name_size == 0) {
        return;
    }
    name[name_size - 1] = '\0';
    for (size_t i = 0; i < name_size - 1; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == '\0') {
            return;
        }
        if (c < 32 || c >= 127) {
            name[i] = '\0';
            return;
        }
    }
}

static bool zephyr_read_u8_field(uint32_t thread_addr, const char *field_name,
                                 uint32_t fallback_offset, uint8_t *out) {
    if (!out) {
        return false;
    }
    uint8_t value = 0;
    if (inspct_get_field("k_thread", thread_addr, field_name, &value)) {
        *out = value;
        return true;
    }
    if (zephyr_read_thread_memory(thread_addr + fallback_offset, &value, sizeof(value))) {
        *out = value;
        return true;
    }
    return false;
}

static bool zephyr_read_u32_field(uint32_t thread_addr, const char *field_name,
                                  uint32_t fallback_offset, uint32_t *out) {
    if (!out) {
        return false;
    }
    uint32_t value = 0;
    if (inspct_get_field("k_thread", thread_addr, field_name, &value)) {
        *out = value;
        return true;
    }
    if (zephyr_read_thread_memory(thread_addr + fallback_offset, &value, sizeof(value))) {
        *out = value;
        return true;
    }
    return false;
}

static bool zephyr_read_current_thread_addr(uint32_t *thread_addr) {
    if (!thread_addr) {
        return false;
    }

    uint32_t kernel_addr = inspct_get_symbol("_kernel");
    if (kernel_addr == 0) {
        return false;
    }

    uint32_t current_offset = inspct_get_field_offset("_cpu", "current");
    if (current_offset == 0) {
        current_offset = ZEPHYR_FALLBACK_CPU_CURRENT_OFFSET;
    }

    uint32_t current = 0;
    if (!zephyr_read_thread_memory(kernel_addr + current_offset, &current, sizeof(current))) {
        return false;
    }
    if (current == 0 || !zephyr_is_ram_addr(current, sizeof(uint32_t))) {
        return false;
    }

    *thread_addr = current;
    return true;
}

static void zephyr_read_thread_name(uint32_t thread_addr, char *name, size_t name_size) {
    if (!name || name_size == 0) {
        return;
    }
    memset(name, 0, name_size);
    if (inspct_get_field("k_thread", thread_addr, "name", name)) {
        zephyr_sanitize_string(name, name_size);
        return;
    }
    zephyr_read_memory(thread_addr + ZEPHYR_FALLBACK_THREAD_NAME_OFFSET,
                       name, (uint32_t)(name_size - 1));
    zephyr_sanitize_string(name, name_size);
}

static bool zephyr_extract_thread_info(uint32_t thread_addr, ZephyrThreadState *out) {
    if (thread_addr == 0 || out == NULL) {
        return false;
    }
    if (!zephyr_is_ram_addr(thread_addr, sizeof(uint32_t))) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->thread_addr = thread_addr;
    zephyr_read_thread_name(thread_addr, out->name, sizeof(out->name));

    uint8_t state = 0;
    if (zephyr_read_u8_field(thread_addr, "base.thread_state",
                             ZEPHYR_FALLBACK_THREAD_STATE_OFFSET, &state)) {
        out->state = state;
    }

    uint8_t prio = 0;
    if (zephyr_read_u8_field(thread_addr, "base.prio",
                             ZEPHYR_FALLBACK_THREAD_PRIO_OFFSET, &prio)) {
        out->priority = (int32_t)(int8_t)prio;
    }

    zephyr_read_u32_field(thread_addr, "base.pended_on",
                          ZEPHYR_FALLBACK_THREAD_PENDED_ON_OFFSET, &out->pended_on);
    return true;
}

static void zephyr_upsert_thread(const ZephyrThreadState *thread, bool scheduled) {
    if (!thread || thread->thread_addr == 0) {
        return;
    }
    for (size_t i = 0; i < all_threads_count; i++) {
        if (all_threads[i].thread_addr == thread->thread_addr) {
            uint64_t prev_count = all_threads[i].scheduled_count;
            all_threads[i] = *thread;
            all_threads[i].scheduled_count = prev_count + (scheduled ? 1 : 0);
            return;
        }
    }
    if (all_threads_count < sizeof(all_threads) / sizeof(all_threads[0])) {
        all_threads[all_threads_count] = *thread;
        all_threads[all_threads_count].scheduled_count = scheduled ? 1 : 0;
        all_threads_count++;
    }
}

static void zephyr_record_switch(const char *direction, const ZephyrThreadState *thread) {
    if (!thread) {
        return;
    }
    uint32_t limit = zephyr_ring_limit();
    switch_count++;
    if (switch_ring_count < limit) {
        switch_ring_count++;
    }
    uint32_t idx = (uint32_t)((switch_count - 1) % limit);
    ZephyrSwitchEvent *event = &switch_ring[idx];
    memset(event, 0, sizeof(*event));
    event->seq = switch_count;
    event->icount = core_get_icount();
    snprintf(event->direction, sizeof(event->direction), "%s", direction ? direction : "");
    event->thread = *thread;
}

static bool zephyr_refresh_current_thread(void) {
    uint32_t thread_addr = 0;
    ZephyrThreadState thread;
    if (!zephyr_read_current_thread_addr(&thread_addr) ||
        !zephyr_extract_thread_info(thread_addr, &thread)) {
        return false;
    }
    current_thread = thread;
    have_current_thread = true;
    zephyr_upsert_thread(&thread, false);
    return true;
}

static void zephyr_handle_switch(const char *direction, bool scheduled) {
    uint32_t thread_addr = 0;
    ZephyrThreadState thread;
    if (!zephyr_read_current_thread_addr(&thread_addr) ||
        !zephyr_extract_thread_info(thread_addr, &thread)) {
        if (zephyr_debug_enabled()) {
            fprintf(stderr, "[Zephyr] Failed to sample current thread\n");
        }
        return;
    }

    zephyr_record_switch(direction, &thread);
    zephyr_upsert_thread(&thread, scheduled);
    if (scheduled) {
        current_thread = thread;
        have_current_thread = true;
    }

    if (zephyr_debug_enabled()) {
        printf("[Zephyr] switch-%s: %s prio=%d state=%u thread=0x%08X pended_on=0x%08X\n",
               direction,
               thread.name[0] ? thread.name : "(unnamed)",
               (int)thread.priority,
               (unsigned)thread.state,
               thread.thread_addr,
               thread.pended_on);
        fflush(stdout);
    }
}

void inspct_zephyr_thread_switched_in(unsigned int cpu_idx, void *arg) {
    (void)cpu_idx;
    (void)arg;
    zephyr_handle_switch("in", true);
}

void inspct_zephyr_thread_switched_out(unsigned int cpu_idx, void *arg) {
    (void)cpu_idx;
    (void)arg;
    zephyr_handle_switch("out", false);
}

static void zephyr_write_thread_json(FILE *f, const ZephyrThreadState *thread) {
    fprintf(f, "{");
    fprintf(f, "\"addr\":\"0x%08X\",", thread ? thread->thread_addr : 0);
    fprintf(f, "\"name\":");
    json_write_escaped(f, thread && thread->name[0] ? thread->name : "");
    fprintf(f, ",\"priority\":%d", thread ? (int)thread->priority : 0);
    fprintf(f, ",\"state\":%u", thread ? (unsigned)thread->state : 0);
    fprintf(f, ",\"pended_on\":\"0x%08X\"", thread ? thread->pended_on : 0);
    fprintf(f, ",\"scheduled_count\":%" PRIu64, thread ? thread->scheduled_count : 0);
    fprintf(f, "}");
}

static void zephyr_write_switch_json(FILE *f, const ZephyrSwitchEvent *event) {
    fprintf(f, "{");
    fprintf(f, "\"seq\":%" PRIu64, event->seq);
    fprintf(f, ",\"icount\":%" PRIu64, event->icount);
    fprintf(f, ",\"direction\":");
    json_write_escaped(f, event->direction);
    fprintf(f, ",\"thread\":");
    zephyr_write_thread_json(f, &event->thread);
    fprintf(f, "}");
}

static uint64_t zephyr_recent_start_seq(uint32_t desired) {
    if (switch_count == 0) {
        return 1;
    }
    uint32_t available = switch_ring_count;
    uint32_t count = desired < available ? desired : available;
    return switch_count - count + 1;
}

static void zephyr_write_recent_switches_array(FILE *f, uint32_t desired) {
    uint32_t limit = zephyr_ring_limit();
    uint64_t start_seq = zephyr_recent_start_seq(desired);
    fprintf(f, "[");
    bool first = true;
    for (uint64_t seq = start_seq; seq <= switch_count && switch_ring_count > 0; seq++) {
        const ZephyrSwitchEvent *event = &switch_ring[(seq - 1) % limit];
        if (event->seq != seq) {
            continue;
        }
        if (!first) {
            fprintf(f, ",");
        }
        zephyr_write_switch_json(f, event);
        first = false;
    }
    fprintf(f, "]");
}

bool inspct_zephyr_write_probe_json(FILE *f, const char *indent) {
    if (!inspct_is_enabled() || !f) {
        return false;
    }
    zephyr_refresh_current_thread();

    const char *p = indent ? indent : "";
    fprintf(f, "%s\"rtos\": {\n", p);
    fprintf(f, "%s  \"name\": \"Zephyr\",\n", p);
    fprintf(f, "%s  \"mode\": ", p);
    json_write_escaped(f, inspct_get_mode());
    fprintf(f, ",\n");
    fprintf(f, "%s  \"switch_count\": %" PRIu64 ",\n", p, switch_count);
    fprintf(f, "%s  \"stored_switch_count\": %u,\n", p, switch_ring_count);
    fprintf(f, "%s  \"thread_count\": %zu,\n", p, all_threads_count);
    fprintf(f, "%s  \"summary_path\": ", p);
    char summary_path[1024];
    snprintf(summary_path, sizeof(summary_path), "%s/rtos_summary.json", inspct_get_out_dir());
    json_write_escaped(f, summary_path);
    fprintf(f, ",\n");
    fprintf(f, "%s  \"recent_switches_path\": ", p);
    char recent_path[1024];
    snprintf(recent_path, sizeof(recent_path), "%s/rtos_recent_switches.jsonl", inspct_get_out_dir());
    json_write_escaped(f, recent_path);
    fprintf(f, ",\n");
    fprintf(f, "%s  \"current_thread\": ", p);
    if (have_current_thread) {
        zephyr_write_thread_json(f, &current_thread);
    } else {
        fprintf(f, "null");
    }
    fprintf(f, ",\n");
    fprintf(f, "%s  \"recent_switches\": ", p);
    zephyr_write_recent_switches_array(f, ZEPHYR_PROBE_RECENT_SWITCHES);
    fprintf(f, "\n%s}", p);
    return true;
}

void inspct_zephyr_write_summary_file(void) {
    if (!inspct_is_enabled()) {
        return;
    }
    zephyr_refresh_current_thread();

    char summary_path[1024];
    snprintf(summary_path, sizeof(summary_path), "%s/rtos_summary.json", inspct_get_out_dir());
    FILE *summary = fopen(summary_path, "w");
    if (summary) {
        fprintf(summary, "{\n");
        fprintf(summary, "  \"name\": \"Zephyr\",\n");
        fprintf(summary, "  \"mode\": ");
        json_write_escaped(summary, inspct_get_mode());
        fprintf(summary, ",\n");
        fprintf(summary, "  \"switch_count\": %" PRIu64 ",\n", switch_count);
        fprintf(summary, "  \"stored_switch_count\": %u,\n", switch_ring_count);
        fprintf(summary, "  \"thread_count\": %zu,\n", all_threads_count);
        fprintf(summary, "  \"current_thread\": ");
        if (have_current_thread) {
            zephyr_write_thread_json(summary, &current_thread);
        } else {
            fprintf(summary, "null");
        }
        fprintf(summary, ",\n");
        fprintf(summary, "  \"threads\": [\n");
        for (size_t i = 0; i < all_threads_count; i++) {
            fprintf(summary, "    ");
            zephyr_write_thread_json(summary, &all_threads[i]);
            fprintf(summary, "%s\n", (i + 1 == all_threads_count) ? "" : ",");
        }
        fprintf(summary, "  ],\n");
        fprintf(summary, "  \"recent_switches\": ");
        zephyr_write_recent_switches_array(summary, ZEPHYR_PROBE_RECENT_SWITCHES);
        fprintf(summary, "\n}\n");
        fclose(summary);
    }

    char recent_path[1024];
    snprintf(recent_path, sizeof(recent_path), "%s/rtos_recent_switches.jsonl", inspct_get_out_dir());
    FILE *recent = fopen(recent_path, "w");
    if (recent) {
        uint32_t limit = zephyr_ring_limit();
        uint64_t start_seq = zephyr_recent_start_seq(switch_ring_count);
        for (uint64_t seq = start_seq; seq <= switch_count && switch_ring_count > 0; seq++) {
            const ZephyrSwitchEvent *event = &switch_ring[(seq - 1) % limit];
            if (event->seq != seq) {
                continue;
            }
            zephyr_write_switch_json(recent, event);
            fprintf(recent, "\n");
        }
        fclose(recent);
    }
}

int inspct_zephyr_init(int argc, char **argv) {
    (void)argc;
    (void)argv;
    virtual_register("zephyr_thread_switched_in_Hook", inspct_zephyr_thread_switched_in);
    virtual_register("zephyr_thread_switched_out_Hook", inspct_zephyr_thread_switched_out);
    return 0;
}
