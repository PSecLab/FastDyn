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
#include "inspct.h"
#include "virtuals.h"

#define CHIBIOS_TASK_NAME_MAX 32

typedef struct {
    uint32_t thread_addr;
    char     name[CHIBIOS_TASK_NAME_MAX];
    uint32_t priority;
    uint32_t state;
    uint32_t owner;
} ChibiOSThreadState;

static ChibiOSThreadState all_threads[64];
static size_t all_threads_count = 0;

/* Try both possible DWARF struct names for the thread descriptor */
static const char *const thread_struct_names[] = { "ch_thread", "thread_t", NULL };

void inspct_chibios_dump_ready_list(uint32_t ch_system_addr);

void inspct_chibios_trace_switch(unsigned int cpu_idx, void *arg);

static bool chibios_extract_thread_info(uint32_t thread_addr, ChibiOSThreadState *out) {
    if (thread_addr == 0 || out == NULL) return false;

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
        qemu_plugin_read_memory(thread_addr + 8, (uint8_t *)&prio_byte, sizeof(prio_byte));
        out->priority = prio_byte;
    }
    for (int i = 0; thread_struct_names[i] != NULL; i++) {
        if (inspct_get_field(thread_struct_names[i], thread_addr, "name", out->name))
            break;
    }
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
        fprintf(stderr, "[ChibiOS] Failed to extract thread info for switch: ntp=0x%08X otp=0x%08X\n", ntp, otp);
        return;
    }

    printf("[ChibiOS] Switch out: %s (Prio %u) -> in: %s (Prio %u) | ntp=0x%08X otp=0x%08X\n",
           out_task.name[0] ? out_task.name : "(null)",
           (unsigned)out_task.priority,
           in_task.name[0] ? in_task.name : "(null)",
           (unsigned)in_task.priority,
           ntp, otp);

    // if new name add to all_threads
    bool found = false;
    for (size_t i = 0; i < all_threads_count; i++) {
        if (all_threads[i].thread_addr == in_task.thread_addr) {
            found = true;
            if (strcmp(all_threads[i].name, in_task.name) != 0) {
                // append the name change to a file
                FILE *f = fopen("/root/rooney/FastDyn/threads_copter.txt", "a");
                if (f) {
                    fprintf(f, "Thread 0x%08X renamed: %s -> %s\n",
                            in_task.thread_addr,
                            all_threads[i].name[0] ? all_threads[i].name : "(null)",
                            in_task.name[0] ? in_task.name : "(null)");
                    fclose(f);
                }
            }
            // if priority changed, log it
            if (all_threads[i].priority != in_task.priority) {
                FILE *f = fopen("/root/rooney/FastDyn/threads_copter.txt", "a");
                if (f) {
                    fprintf(f, "Thread %s priority changed: %u -> %u\n",
                            in_task.name[0] ? in_task.name : "(null)",
                            (unsigned)all_threads[i].priority,
                            (unsigned)in_task.priority);
                    fclose(f);
                }
            }

            all_threads[i] = in_task;
            break;
        }
    }
    if (!found && all_threads_count < sizeof(all_threads) / sizeof(all_threads[0])) {
        all_threads[all_threads_count++] = in_task;
        // append the name to a file
        FILE *f = fopen("/root/rooney/FastDyn/threads_copter.txt", "a");
        if (f) {
            fprintf(f, "New thread: %s, Prio: %u\n", in_task.name[0] ? in_task.name : "(null)", (unsigned)in_task.priority);
            fclose(f);
        }
    }

    uint32_t ch_system_addr = inspct_get_symbol("ch_system");
    if (ch_system_addr != 0)
        inspct_chibios_dump_ready_list(ch_system_addr);

    fflush(stdout);
}

/* Thread creation: __thd_object_init(oip, tp, name, prio) — R0=oip, R1=tp, R2=name, R3=prio */
void inspct_chibios_thd_object_init(unsigned int cpu_idx, void *arg) {
    (void)cpu_idx;
    (void)arg;

    uint32_t tp = (uint32_t)qemu_get_register(ARM_V7M_R1);
    if (tp == 0) return;

    ChibiOSThreadState task;
    if (!chibios_extract_thread_info(tp, &task)) return;

    printf("[ChibiOS] [+] New thread registered: %s | Prio: %u | thread_t=0x%08X\n",
           task.name[0] ? task.name : "(null)",
           (unsigned)task.priority,
           tp);
    fflush(stdout);
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


int inspct_chibios_init(int argc, char ** argv){
	int status =0;
	virtual_register("__port_switch_Hook", inspct_chibios_trace_switch);
    virtual_register("__trace_switch_Hook", inspct_chibios_trace_switch);
    virtual_register("__thd_object_init_Hook", inspct_chibios_thd_object_init);
	return status;
}
