/*
 * ChibiOS/RT external introspection hooks.
 * Hook __trace_switch for context switches, __thd_object_init for thread
 * registration, and dump the ready list via ch_system -> os_instance -> rlist.
 *
 * Schema must include structs: ch_thread (thread_t), ch_system (ch_system_t),
 * ch_os_instance (os_instance_t). Symbols: ch_system, __trace_switch,
 * __thd_object_init.
 * ARM AAPCS: R0=1st arg, R1=2nd, R2=3rd, R3=4th.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>
#include <core.h>
#include "inspct.h"
#include "virtuals.h"

#define CHIBIOS_TASK_NAME_MAX 32
#define CHIBIOS_SWITCH_RING_CAP 16384
#define CHIBIOS_PROBE_RECENT_SWITCHES 8

typedef struct {
    uint32_t thread_addr;
    char     name[CHIBIOS_TASK_NAME_MAX];
    uint32_t priority;
    uint32_t state;
    uint32_t owner;
    uint64_t scheduled_count;
} ChibiOSThreadState;

static ChibiOSThreadState all_threads[64];
static size_t all_threads_count = 0;

typedef struct {
    uint64_t seq;
    uint64_t icount;
    uint32_t in_thread;
    uint32_t out_thread;
    char in_name[CHIBIOS_TASK_NAME_MAX];
    char out_name[CHIBIOS_TASK_NAME_MAX];
    uint32_t in_priority;
    uint32_t out_priority;
    uint32_t in_state;
    uint32_t out_state;
} ChibiOSSwitchEvent;

static ChibiOSSwitchEvent switch_ring[CHIBIOS_SWITCH_RING_CAP];
static uint64_t switch_count = 0;
static uint32_t switch_ring_count = 0;
static ChibiOSThreadState current_thread;
static bool have_current_thread = false;

/* Try both possible DWARF struct names for the thread descriptor */
static const char *const thread_struct_names[] = { "ch_thread", "thread_t", NULL };

void inspct_chibios_dump_ready_list(uint32_t ch_system_addr);

void inspct_chibios_trace_switch(unsigned int cpu_idx, void *arg);

static bool chibios_debug_enabled(void) {
    const char *mode = inspct_get_mode();
    return mode && strcmp(mode, "debug") == 0;
}

static uint32_t chibios_ring_limit(void) {
    uint32_t limit = inspct_get_max_events();
    if (limit == 0) {
        limit = 1;
    }
    if (limit > CHIBIOS_SWITCH_RING_CAP) {
        limit = CHIBIOS_SWITCH_RING_CAP;
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

static bool chibios_range_contains(uint32_t addr, uint32_t size,
                                   uint32_t start, uint32_t end) {
    if (size == 0 || addr < start || addr >= end) {
        return false;
    }
    uint32_t last = addr + size - 1;
    return last >= addr && last < end;
}

static bool chibios_is_ram_addr(uint32_t addr, uint32_t size) {
    return chibios_range_contains(addr, size, 0x20000000U, 0x20040000U) ||
           chibios_range_contains(addr, size, 0x10000000U, 0x10010000U);
}

static bool chibios_is_string_addr(uint32_t addr, uint32_t size) {
    return chibios_range_contains(addr, size, 0x08000000U, 0x08200000U) ||
           chibios_is_ram_addr(addr, size);
}

static bool chibios_read_memory(uint32_t addr, void *out, uint32_t size) {
    if (!out || size == 0) {
        return false;
    }
    if (!chibios_is_string_addr(addr, size)) {
        return false;
    }
    return qemu_plugin_read_memory(addr, (uint8_t *)out, (int)size) == 0;
}

static bool chibios_read_thread_memory(uint32_t addr, void *out, uint32_t size) {
    if (!chibios_is_ram_addr(addr, size)) {
        return false;
    }
    return qemu_plugin_read_memory(addr, (uint8_t *)out, (int)size) == 0;
}

static void chibios_read_thread_name(uint32_t thread_addr, char *name, size_t name_size) {
    if (!name || name_size == 0) {
        return;
    }
    memset(name, 0, name_size);
    for (int i = 0; thread_struct_names[i] != NULL; i++) {
        uint32_t name_offset = inspct_get_field_offset(thread_struct_names[i], "name");
        if (name_offset == 0) {
            continue;
        }
        uint32_t name_ptr = 0;
        if (!chibios_read_thread_memory(thread_addr + name_offset, &name_ptr, sizeof(name_ptr))) {
            continue;
        }
        if (name_ptr == 0 || !chibios_is_string_addr(name_ptr, 1)) {
            continue;
        }
        chibios_read_memory(name_ptr, name, (uint32_t)(name_size - 1));
        name[name_size - 1] = '\0';
        return;
    }
}

static void chibios_upsert_thread(const ChibiOSThreadState *task, bool scheduled) {
    if (!task || task->thread_addr == 0) {
        return;
    }
    for (size_t i = 0; i < all_threads_count; i++) {
        if (all_threads[i].thread_addr == task->thread_addr) {
            uint64_t prev_count = all_threads[i].scheduled_count;
            all_threads[i] = *task;
            all_threads[i].scheduled_count = prev_count + (scheduled ? 1 : 0);
            return;
        }
    }
    if (all_threads_count < sizeof(all_threads) / sizeof(all_threads[0])) {
        all_threads[all_threads_count] = *task;
        all_threads[all_threads_count].scheduled_count = scheduled ? 1 : 0;
        all_threads_count++;
    }
}

static void chibios_record_switch(const ChibiOSThreadState *in_task,
                                  const ChibiOSThreadState *out_task) {
    uint32_t limit = chibios_ring_limit();
    switch_count++;
    if (switch_ring_count < limit) {
        switch_ring_count++;
    }
    uint32_t idx = (uint32_t)((switch_count - 1) % limit);
    ChibiOSSwitchEvent *event = &switch_ring[idx];
    memset(event, 0, sizeof(*event));
    event->seq = switch_count;
    event->icount = core_get_icount();
    if (in_task) {
        event->in_thread = in_task->thread_addr;
        snprintf(event->in_name, sizeof(event->in_name), "%s", in_task->name);
        event->in_priority = in_task->priority;
        event->in_state = in_task->state;
    }
    if (out_task) {
        event->out_thread = out_task->thread_addr;
        snprintf(event->out_name, sizeof(event->out_name), "%s", out_task->name);
        event->out_priority = out_task->priority;
        event->out_state = out_task->state;
    }
}

static bool chibios_extract_thread_info(uint32_t thread_addr, ChibiOSThreadState *out) {
    if (thread_addr == 0 || out == NULL) return false;
    if (!chibios_is_ram_addr(thread_addr, sizeof(uint32_t))) return false;

    memset(out, 0, sizeof(ChibiOSThreadState));
    out->thread_addr = thread_addr;

    for (int i = 0; thread_struct_names[i] != NULL; i++) {
        const char *sname = thread_struct_names[i];
        if (inspct_get_field(sname, thread_addr, "hdr.pqueue.prio", &out->priority))
            break;
    }
    /* Schema often has union "hdr" only (not flattened to hdr.pqueue.prio). Prio is at offset 8 in ch_priority_queue. */
    if (out->priority == 0) {
        uint8_t prio_byte = 0;
        chibios_read_thread_memory(thread_addr + 8, &prio_byte, sizeof(prio_byte));
        out->priority = prio_byte;
    }
    chibios_read_thread_name(thread_addr, out->name, sizeof(out->name));
    for (int i = 0; thread_struct_names[i] != NULL; i++) {
        if (inspct_get_field(thread_struct_names[i], thread_addr, "state", &out->state))
            break;
    }
    for (int i = 0; thread_struct_names[i] != NULL; i++) {
        if (inspct_get_field(thread_struct_names[i], thread_addr, "owner", &out->owner))
            break;
    }

    return true;
}

/* Context switch: __trace_switch(thread_t *ntp, thread_t *otp) — R0=ntp, R1=otp */
void inspct_chibios_trace_switch(unsigned int cpu_idx, void *arg) {
    (void)cpu_idx;
    (void)arg;

    uint32_t ntp = (uint32_t)qemu_get_register(ARM_V7M_R0);  /* in */
    uint32_t otp = (uint32_t)qemu_get_register(ARM_V7M_R1);  /* out */

    ChibiOSThreadState in_task, out_task;
    if (!chibios_extract_thread_info(ntp, &in_task) ||
        !chibios_extract_thread_info(otp, &out_task))
    {
        if (chibios_debug_enabled()) {
            fprintf(stderr, "[ChibiOS] Failed to extract thread info for switch: ntp=0x%08X otp=0x%08X\n", ntp, otp);
        }
        return;
    }

    chibios_record_switch(&in_task, &out_task);
    chibios_upsert_thread(&out_task, false);
    chibios_upsert_thread(&in_task, true);
    current_thread = in_task;
    have_current_thread = true;

    if (chibios_debug_enabled()) {
        printf("[ChibiOS] Switch out: %s (Prio %u) -> in: %s (Prio %u) | ntp=0x%08X otp=0x%08X\n",
               out_task.name[0] ? out_task.name : "(null)",
               (unsigned)out_task.priority,
               in_task.name[0] ? in_task.name : "(null)",
               (unsigned)in_task.priority,
               ntp, otp);
    }

    uint32_t ch_system_addr = inspct_get_symbol("ch_system");
    if (chibios_debug_enabled() && ch_system_addr != 0)
        inspct_chibios_dump_ready_list(ch_system_addr);

    if (chibios_debug_enabled()) {
        fflush(stdout);
    }
}

/* Thread creation return: __thd_object_init(...) returns initialized thread_t * in R0. */
void inspct_chibios_thd_object_init(unsigned int cpu_idx, void *arg) {
    (void)cpu_idx;
    (void)arg;

    uint32_t tp = (uint32_t)qemu_get_register(ARM_V7M_R0);
    if (tp == 0) return;

    ChibiOSThreadState task;
    if (!chibios_extract_thread_info(tp, &task)) return;

    chibios_upsert_thread(&task, false);

    if (chibios_debug_enabled()) {
        printf("[ChibiOS] [+] New thread registered: %s | Prio: %u | thread_t=0x%08X\n",
               task.name[0] ? task.name : "(null)",
               (unsigned)task.priority,
               tp);
        fflush(stdout);
    }
}

/* Walk the ready list (priority queue) of one OS instance. Each element is
 * thread_t.hdr.pqueue; list header is rlist.pqueue. */
static void chibios_walk_ready_pqueue(uint32_t pqueue_header_addr, uint32_t current_tp,
                                      const char *inst_label) {
    /* thread_t.hdr.pqueue offset. Schema often has only "hdr" (union); then hdr.pqueue is missing and we use 0. */
    uint32_t off_pqueue = inspct_get_field_offset("ch_thread", "hdr.pqueue");
    /* 0 is valid: pqueue is at start of thread_t */

    /* ch_priority_queue_t: first field is next (list head) */
    uint32_t next_ptr = 0;
    qemu_plugin_read_memory(pqueue_header_addr, (uint8_t *)&next_ptr, sizeof(next_ptr));

    uint32_t head = pqueue_header_addr;
    uint32_t count = 0;
    const uint32_t max_walk = 64;

    while (next_ptr != 0 && next_ptr != head && count < max_walk) {
        uint32_t thread_base = next_ptr - off_pqueue;
        ChibiOSThreadState task;
        if (chibios_extract_thread_info(thread_base, &task)) {
            const char *cur = (thread_base == current_tp) ? " [CURRENT]" : "";
            printf("[ChibiOS]   [%s] %s | Prio %u | 0x%08X%s\n",
                   inst_label,
                   task.name[0] ? task.name : "(null)",
                   (unsigned)task.priority,
                   thread_base, cur);
        }

        uint32_t elem_next = 0;
        qemu_plugin_read_memory(next_ptr, (uint8_t *)&elem_next, sizeof(elem_next));
        next_ptr = elem_next;
        count++;
    }
}

void inspct_chibios_dump_ready_list(uint32_t ch_system_addr) {
    if (ch_system_addr == 0) return;

    uint32_t instances_offset = inspct_get_field_offset("ch_system", "instances");
    if (instances_offset == 0) return;

    uint32_t inst0 = 0;
    qemu_plugin_read_memory(ch_system_addr + instances_offset, (uint8_t *)&inst0, sizeof(inst0));
    if (inst0 == 0) return;

    uint32_t rlist_current_offset = inspct_get_field_offset("ch_os_instance", "rlist.current");
    /* Schema flattens to rlist.pqueue.next (at 0); pqueue header is at same address as rlist. */
    uint32_t rlist_pqueue_offset  = inspct_get_field_offset("ch_os_instance", "rlist.pqueue.next");
    if (rlist_pqueue_offset == 0)
        rlist_pqueue_offset = inspct_get_field_offset("ch_os_instance", "rlist.pqueue");
    if (rlist_pqueue_offset == 0)
        rlist_pqueue_offset = inspct_get_field_offset("ch_os_instance", "rlist");

    uint32_t current_tp = 0;
    if (rlist_current_offset != 0)
        qemu_plugin_read_memory(inst0 + rlist_current_offset, (uint8_t *)&current_tp, sizeof(current_tp));

    uint32_t pqueue_addr = inst0 + rlist_pqueue_offset;

    printf("[ChibiOS] === Ready list (instance 0) ===\n");
    chibios_walk_ready_pqueue(pqueue_addr, current_tp, "ready");
    // printf("[ChibiOS] Current: 0x%08X\n", current_tp);
    printf("[ChibiOS] =================================\n");
    fflush(stdout);
}

static void chibios_write_thread_json(FILE *f, const ChibiOSThreadState *task) {
    fprintf(f, "{");
    fprintf(f, "\"addr\":\"0x%08X\",", task ? task->thread_addr : 0);
    fprintf(f, "\"name\":");
    json_write_escaped(f, task && task->name[0] ? task->name : "");
    fprintf(f, ",\"priority\":%u", task ? (unsigned)task->priority : 0);
    fprintf(f, ",\"state\":%u", task ? (unsigned)task->state : 0);
    fprintf(f, ",\"owner\":\"0x%08X\"", task ? task->owner : 0);
    fprintf(f, ",\"scheduled_count\":%" PRIu64, task ? task->scheduled_count : 0);
    fprintf(f, "}");
}

static void chibios_write_switch_json(FILE *f, const ChibiOSSwitchEvent *event) {
    fprintf(f, "{");
    fprintf(f, "\"seq\":%" PRIu64, event->seq);
    fprintf(f, ",\"icount\":%" PRIu64, event->icount);
    fprintf(f, ",\"out\":{\"addr\":\"0x%08X\",\"name\":", event->out_thread);
    json_write_escaped(f, event->out_name);
    fprintf(f, ",\"priority\":%u,\"state\":%u}", event->out_priority, event->out_state);
    fprintf(f, ",\"in\":{\"addr\":\"0x%08X\",\"name\":", event->in_thread);
    json_write_escaped(f, event->in_name);
    fprintf(f, ",\"priority\":%u,\"state\":%u}", event->in_priority, event->in_state);
    fprintf(f, "}");
}

static uint64_t chibios_recent_start_seq(uint32_t desired) {
    if (switch_count == 0) {
        return 1;
    }
    uint32_t available = switch_ring_count;
    uint32_t count = desired < available ? desired : available;
    return switch_count - count + 1;
}

static void chibios_write_recent_switches_array(FILE *f, uint32_t desired) {
    uint32_t limit = chibios_ring_limit();
    uint64_t start_seq = chibios_recent_start_seq(desired);
    fprintf(f, "[");
    bool first = true;
    for (uint64_t seq = start_seq; seq <= switch_count && switch_ring_count > 0; seq++) {
        const ChibiOSSwitchEvent *event = &switch_ring[(seq - 1) % limit];
        if (event->seq != seq) {
            continue;
        }
        if (!first) {
            fprintf(f, ",");
        }
        chibios_write_switch_json(f, event);
        first = false;
    }
    fprintf(f, "]");
}

bool inspct_chibios_write_probe_json(FILE *f, const char *indent) {
    if (!inspct_is_enabled() || !f) {
        return false;
    }
    const char *p = indent ? indent : "";
    fprintf(f, "%s\"rtos\": {\n", p);
    fprintf(f, "%s  \"name\": \"ChibiOS\",\n", p);
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
        chibios_write_thread_json(f, &current_thread);
    } else {
        fprintf(f, "null");
    }
    fprintf(f, ",\n");
    fprintf(f, "%s  \"recent_switches\": ", p);
    chibios_write_recent_switches_array(f, CHIBIOS_PROBE_RECENT_SWITCHES);
    fprintf(f, "\n%s}", p);
    return true;
}

void inspct_chibios_write_summary_file(void) {
    if (!inspct_is_enabled()) {
        return;
    }

    char summary_path[1024];
    snprintf(summary_path, sizeof(summary_path), "%s/rtos_summary.json", inspct_get_out_dir());
    FILE *summary = fopen(summary_path, "w");
    if (summary) {
        fprintf(summary, "{\n");
        fprintf(summary, "  \"name\": \"ChibiOS\",\n");
        fprintf(summary, "  \"mode\": ");
        json_write_escaped(summary, inspct_get_mode());
        fprintf(summary, ",\n");
        fprintf(summary, "  \"switch_count\": %" PRIu64 ",\n", switch_count);
        fprintf(summary, "  \"stored_switch_count\": %u,\n", switch_ring_count);
        fprintf(summary, "  \"thread_count\": %zu,\n", all_threads_count);
        fprintf(summary, "  \"current_thread\": ");
        if (have_current_thread) {
            chibios_write_thread_json(summary, &current_thread);
        } else {
            fprintf(summary, "null");
        }
        fprintf(summary, ",\n");
        fprintf(summary, "  \"threads\": [\n");
        for (size_t i = 0; i < all_threads_count; i++) {
            fprintf(summary, "    ");
            chibios_write_thread_json(summary, &all_threads[i]);
            fprintf(summary, "%s\n", (i + 1 == all_threads_count) ? "" : ",");
        }
        fprintf(summary, "  ],\n");
        fprintf(summary, "  \"recent_switches\": ");
        chibios_write_recent_switches_array(summary, CHIBIOS_PROBE_RECENT_SWITCHES);
        fprintf(summary, "\n}\n");
        fclose(summary);
    }

    char recent_path[1024];
    snprintf(recent_path, sizeof(recent_path), "%s/rtos_recent_switches.jsonl", inspct_get_out_dir());
    FILE *recent = fopen(recent_path, "w");
    if (recent) {
        uint32_t limit = chibios_ring_limit();
        uint64_t start_seq = chibios_recent_start_seq(switch_ring_count);
        for (uint64_t seq = start_seq; seq <= switch_count && switch_ring_count > 0; seq++) {
            const ChibiOSSwitchEvent *event = &switch_ring[(seq - 1) % limit];
            if (event->seq != seq) {
                continue;
            }
            chibios_write_switch_json(recent, event);
            fprintf(recent, "\n");
        }
        fclose(recent);
    }
}


int inspct_chibios_init(int argc, char ** argv){
	int status =0;
	virtual_register("__port_switch_Hook", inspct_chibios_trace_switch);
    virtual_register("__trace_switch_Hook", inspct_chibios_trace_switch);
    virtual_register("__thd_object_init_Hook", inspct_chibios_thd_object_init);
	return status;
}
