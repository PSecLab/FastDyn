/**
 * @brief Reusable virtuals for ardupilot
 *
 * This file implements the common virtual functions used by ardupilot.
 *
 * @file FastDyn/virtuals/ardupilot_virtuals.c
 * @author Michael Rooney
 */

#include "virtuals.h"

static volatile char * storage_memory = NULL;
static const size_t storage_size = 32 * 1024; // 32KB of simulated storage

void storage_read_block(unsigned int cpu_index, void *udata) {
    uint8_t * temp_buffer = NULL;

    if (storage_memory == NULL) {
        storage_memory = malloc(storage_size);
        if (storage_memory == NULL) {
            fprintf(stderr, "Failed to allocate storage memory\n");
            goto end;
        }
        memset((void*)storage_memory, 0xFF, storage_size); // Initialize to 0xFF
    }

    uint32_t dst = qemu_get_register(ARM_V7M_R1);
    uint16_t loc = (uint16_t)qemu_get_register(ARM_V7M_R2);
    size_t size = (size_t)qemu_get_register(ARM_V7M_R3);

    if (loc + size > storage_size) {
        fprintf(stderr, "Storage read out of bounds\n");
        fprintf(stderr, "Offset: %u, Size: %lu, Storage Size: %zu\n", loc, size, storage_size);
        goto end;
    }

    temp_buffer = malloc(size);
    if (!temp_buffer) {
        fprintf(stderr, "Failed to allocate temporary buffer\n");
        goto end;
    }

    printf("storage_read_block: offset=0x%08x, address=0x%08x, size=%lu\n", loc, dst, size);

    memcpy(temp_buffer, (void*)(storage_memory + loc), size);

    qemu_plugin_write_memory(dst, temp_buffer, size);

    // return from function and return 0
end:
    qemu_set_register(0, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
    if (temp_buffer) {
        free(temp_buffer);
    }
}

void storage_write_block(unsigned int cpu_index, void *udata) {
    uint8_t * temp_buffer = NULL;

    if (storage_memory == NULL) {
        storage_memory = malloc(storage_size);
        if (storage_memory == NULL) {
            fprintf(stderr, "Failed to allocate storage memory\n");
            goto end;
        }
        memset((void*)storage_memory, 0xFF, storage_size); // Initialize to 0xFF
    }

    uint16_t loc = (uint16_t)qemu_get_register(ARM_V7M_R1);
    uint32_t src = qemu_get_register(ARM_V7M_R2);
    size_t size = (size_t)qemu_get_register(ARM_V7M_R3);

    if (loc + size > storage_size) {
        fprintf(stderr, "Storage write out of bounds\n");
        goto end;
    }

    temp_buffer = malloc(size);
    if (!temp_buffer) {
        fprintf(stderr, "Failed to allocate temporary buffer\n");
        goto end;
    }

    qemu_plugin_read_memory(src, temp_buffer, size);

    memcpy((void*)(storage_memory + loc), temp_buffer, size);

    // return from function and return 0
end:
    qemu_set_register(0, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
    if (temp_buffer) {
        free(temp_buffer);
    }
}

void hrt_micros64(unsigned int cpu_index, void *udata) {
    int64_t nanos = qemu_plugin_get_virtual_timer();

    if (nanos < 0) {
        fprintf(stderr, "Error getting virtual timer\n");
        return;
    }

    uint64_t micros = nanos / 1000;
    uint32_t micros_upper_32 = (uint32_t)(micros >> 32);
    uint32_t micros_lower_32 = (uint32_t)(micros & 0xFFFFFFFF);

    qemu_set_register(micros_upper_32, ARM_V7M_R0);
    qemu_set_register(micros_lower_32, ARM_V7M_R1);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

void ap_hal_micros32(unsigned int cpu_index, void *udata) {
    int64_t nanos = qemu_plugin_get_virtual_timer();

    printf("ap_hal_micros32 called, nanos: %ld\n", nanos);

    if (nanos < 0) {
        fprintf(stderr, "Error getting virtual timer\n");
        return;
    }

    uint32_t micros = (uint32_t)(nanos / 1000);

    qemu_set_register(micros, ARM_V7M_R0);
}

void chDbgContextSwitching(unsigned int cpu_index, void *udata) {
    uint32_t thread1 = qemu_get_register(ARM_V7M_R0);
    uint32_t thread2 = qemu_get_register(ARM_V7M_R1);

    uint32_t thread1_name_offset = thread1 + 0x1c;
    uint32_t thread2_name_offset = thread2 + 0x1c;

    uint32_t thread1_name_ptr = 0;
    uint32_t thread2_name_ptr = 0;

    qemu_plugin_read_memory(thread1_name_offset, (uint8_t*)&thread1_name_ptr, sizeof(uint32_t));
    qemu_plugin_read_memory(thread2_name_offset, (uint8_t*)&thread2_name_ptr, sizeof(uint32_t));

    char thread1_name[17] = {0};
    char thread2_name[17] = {0};

    qemu_plugin_read_memory(thread1_name_ptr, (uint8_t*)thread1_name, sizeof(thread1_name));
    qemu_plugin_read_memory(thread2_name_ptr, (uint8_t*)thread2_name, sizeof(thread2_name));

    printf("Switching context: %s -> %s\n", thread2_name, thread1_name);

    printf("\nRegisters before switch:\n");

    // uint32_t thread1_ctx_offset = thread1 + 0xc;
    // uint32_t thread2_ctx_offset = thread2 + 0xc;

    // port_context_t ctx1;
    // port_context_t ctx2;

    // qemu_plugin_read_memory(thread1_ctx_offset, (uint8_t*)&ctx1.intctx, sizeof(port_intctx_t));
    // qemu_plugin_read_memory(thread2_ctx_offset, (uint8_t*)&ctx2.intctx, sizeof(port_intctx_t));

    // printf("R4: 0x%08x -> 0x%08x\n", ctx2.intctx.r4, ctx1.intctx.r4);
    // printf("R5: 0x%08x -> 0x%08x\n", ctx2.intctx.r5, ctx1.intctx.r5);
    // printf("R6: 0x%08x -> 0x%08x\n", ctx2.intctx.r6, ctx1.intctx.r6);
    // printf("R7: 0x%08x -> 0x%08x\n", ctx2.intctx.r7, ctx1.intctx.r7);
    // printf("R8: 0x%08x -> 0x%08x\n", ctx2.intctx.r8, ctx1.intctx.r8);
    // printf("R9: 0x%08x -> 0x%08x\n", ctx2.intctx.r9, ctx1.intctx.r9);
    // printf("R10: 0x%08x -> 0x%08x\n", ctx2.intctx.r10, ctx1.intctx.r10);
    // printf("R11: 0x%08x -> 0x%08x\n", ctx2.intctx.r11, ctx1.intctx.r11);
    // printf("LR: 0x%08x -> 0x%08x\n", ctx2.intctx.lr, ctx1.intctx.lr);

    printf("\n");
}

void debug_reached(unsigned int cpu_index, void *udata) {
    printf("Debug: Reached debug_reached virtual function\n");
}

// END OF FILE
