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

    uint32_t offset = qemu_get_register(ARM_V7M_R1);
    uint32_t address = qemu_get_register(ARM_V7M_R2);
    uint32_t size = qemu_get_register(ARM_V7M_R3);

    if (offset + size > storage_size) {
        fprintf(stderr, "Storage read out of bounds\n");
        fprintf(stderr, "Offset: %u, Size: %u, Storage Size: %zu\n", offset, size, storage_size);
        goto end;
    }

    temp_buffer = malloc(size);
    if (!temp_buffer) {
        fprintf(stderr, "Failed to allocate temporary buffer\n");
        goto end;
    }

    memcpy(temp_buffer, (void*)(storage_memory + offset), size);

    qemu_plugin_write_memory(address, temp_buffer, size);

    // return from function and return 0
    qemu_set_register(0, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
end:
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

    uint32_t offset = qemu_get_register(ARM_V7M_R1);
    uint32_t address = qemu_get_register(ARM_V7M_R2);
    uint32_t size = qemu_get_register(ARM_V7M_R3);

    if (address + size > storage_size) {
        fprintf(stderr, "Storage write out of bounds\n");
        goto end;
    }

    temp_buffer = malloc(size);
    if (!temp_buffer) {
        fprintf(stderr, "Failed to allocate temporary buffer\n");
        goto end;
    }

    qemu_plugin_read_memory(address, temp_buffer, size);

    memcpy((void*)(storage_memory + offset), temp_buffer, size);

    // return from function and return 0
    qemu_set_register(0, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
end:
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
    printf("PC before: 0x%08x\n", qemu_get_register(ARM_V7M_PC));
    printf("LR before: 0x%08x\n", qemu_get_register(ARM_V7M_LR));

    if (nanos < 0) {
        fprintf(stderr, "Error getting virtual timer\n");
        return;
    }

    uint32_t micros = (uint32_t)(nanos / 1000);

    qemu_set_register(micros, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    printf("PC after: 0x%08x\n", qemu_get_register(ARM_V7M_PC));
    printf("LR after: 0x%08x\n", lr);
    qemu_set_register(lr, ARM_V7M_PC);
}