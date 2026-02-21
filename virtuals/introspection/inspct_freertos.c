#include <stdio.h>
#include "inspct.h"
#define configMAX_TASK_NAME_LEN 256
/* ========================================================================= */
/* 5. Execution Hook: Introspecting the Current Task                         */
/* ========================================================================= */
int init =0;
void inspct_freertos_vTaskSwitchContext(unsigned int cpu_idx,  void *arg) {

	if (!init) {
		load_fastdyn_schemas("/home/faculty/abk6349/data/fastdyn/fastdyn_work/schema.txt");
		init =1;
	}

	uint32_t pxCurrentTCB_global_addr = (uint32_t)strtoul(arg, NULL, 16);

	uint32_t active_tcb_addr = 0;
    qemu_plugin_read_memory(pxCurrentTCB_global_addr, (uint8_t*)&active_tcb_addr, sizeof(uint32_t));

    // If the scheduler hasn't started yet, this will be NULL
    if (active_tcb_addr == 0) {
        return;
    }

    uint32_t priority = 0;
    char task_name[configMAX_TASK_NAME_LEN] = {0}; // Usually 16 bytes
    uint32_t owner_ptr = 0;

    // 2. Extract the task priority using the TRUE compiler struct name
    if (inspct_get_field("tskTaskControlBlock", active_tcb_addr, "uxPriority", &priority)) {
        // Priority successfully extracted
    }

    // 3. Extract the inline task name string
    if (inspct_get_field("tskTaskControlBlock", active_tcb_addr, "pcTaskName", task_name)) {
        // Name successfully extracted
    }

    // 4. Extract the flattened nested struct pointer (e.g., to verify list integrity)
    if (inspct_get_field("tskTaskControlBlock", active_tcb_addr, "xStateListItem.pvOwner", &owner_ptr)) {
        // owner_ptr should actually point right back to active_tcb_addr!
    }

    // Example debug print (in a real plugin, you'd send this back to Python via a socket/pipe)
    printf("[FastDyn:FreeRTOS] Task: %s | Prio: %u | TCB: 0x%08X\n", task_name, priority, active_tcb_addr);


    // --- ADVANCED: Walking the State List ---
    // If you wanted to see the next task waiting in whatever list this task is attached to:
    uint32_t next_item_ptr = 0;
    if (inspct_get_field("tskTaskControlBlock", active_tcb_addr, "xStateListItem.pxNext", &next_item_ptr)) {

        if (next_item_ptr != 0) {
            uint32_t next_task_tcb = 0;
            // Notice we switch context to "xLIST_ITEM" to read the node!
            inspct_get_field("xLIST_ITEM", next_item_ptr, "pvOwner", &next_task_tcb);
        }
    }
}

void inspct_freertos_prvAddNewTaskToReadyList(unsigned int cpu_idx, void *arg) {
	 if (!init) {
        load_fastdyn_schemas("/home/faculty/abk6349/data/fastdyn/fastdyn_work/schema.txt");
        init =1;
    }
    // 1. The fully populated TCB pointer is the first argument (R0)
    uint32_t new_tcb_addr = qemu_get_register(0);

    if (new_tcb_addr == 0) return;

    char task_name[32] = {0};
    uint32_t priority = 0;

    // 2. Now we CAN use the schema API, because the struct is fully built in SRAM!
    if (inspct_get_field("tskTaskControlBlock", new_tcb_addr, "uxPriority", &priority)) {
        // Priority extracted
    }

    if (inspct_get_field("tskTaskControlBlock", new_tcb_addr, "pcTaskName", task_name)) {
        // Name extracted
    }

    printf("[FastDyn:FreeRTOS] [+] New Task Registered -> Name: %s | Prio: %u | TCB: 0x%08X\n",
           task_name, priority, new_tcb_addr);
    fflush(stdout);
}
