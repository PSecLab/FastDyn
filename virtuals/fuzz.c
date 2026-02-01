#include <utils.h>
#include <core.h>
#include <common.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "mavlink_lib.h"
#include "core.h"
#include "mavlink.h"
#include "common.h"

extern int coverage;

uint32_t last_anchor_id = -1;

int fuzz_stop();

typedef struct saved_registers_t {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t r12;
    uint32_t sp;
    uint32_t lr;
    uint32_t pc;
} saved_registers_t;

static saved_registers_t g_saved_registers;

static void vcpu_tb_exec(unsigned int vcpu_index, void *userdata)
{
    // userdata is whatever you passed at registration time
}

void virt_assert(unsigned int cpu_index, void *udata)
{
    if (!coverage) {
        utils_die("Coverage not enabled, cannot assert coverage data");
    }

    if (!udata)
        return;

    // printf("Asserted\n");

    // Currently reserving 0xDEADBEEF for a crash, we should have a better systems
    // For example, all exceptions?

    const char *str = (const char *)udata;

    // printf("Assert target: %s\n", str);

    // Expect something like "*0x8003940"
    if (str[0] != '*') {
        fprintf(stderr, "[assert] Invalid format: %s\n", str);
        return;
    }

    // Skip the '*' and parse the rest as hex or decimal
    uint64_t addr = strtoull(str + 1, NULL, 0);
    if (addr != 0) { // set pc to supplied address
        fuzz_report_assert(last_anchor_id, false);
        qemu_set_register(addr, ARM_V7M_PC);
    } else { // for address == 0, perform a reset
        uint32_t pc = qemu_get_register(ARM_V7M_PC);
        printf("PC=0x%08x\n", pc);
        fuzz_report_assert(last_anchor_id, true);
        fuzz_finish(last_anchor_id);
        fuzz_stop();
        while (true); // wait for fuzzer to exit the process
    }
}

static bool save_registers(void)
{
    g_saved_registers.r0  = qemu_get_register(ARM_V7M_R0);
    g_saved_registers.r1  = qemu_get_register(ARM_V7M_R1);
    g_saved_registers.r2  = qemu_get_register(ARM_V7M_R2);
    g_saved_registers.r3  = qemu_get_register(ARM_V7M_R3);
    g_saved_registers.r4  = qemu_get_register(ARM_V7M_R4);
    g_saved_registers.r5  = qemu_get_register(ARM_V7M_R5);
    g_saved_registers.r6  = qemu_get_register(ARM_V7M_R6);
    g_saved_registers.r7  = qemu_get_register(ARM_V7M_R7);
    g_saved_registers.r8  = qemu_get_register(ARM_V7M_R8);
    g_saved_registers.r9  = qemu_get_register(ARM_V7M_R9);
    g_saved_registers.r10 = qemu_get_register(ARM_V7M_R10);
    g_saved_registers.r11 = qemu_get_register(ARM_V7M_R11);
    g_saved_registers.r12 = qemu_get_register(ARM_V7M_R12);
    g_saved_registers.sp  = qemu_get_register(ARM_V7M_SP);
    g_saved_registers.lr  = qemu_get_register(ARM_V7M_LR);
    g_saved_registers.pc  = qemu_get_register(ARM_V7M_PC);
    return true;
}

static bool restore_registers(void)
{
    qemu_set_register(g_saved_registers.r0,  ARM_V7M_R0);
    qemu_set_register(g_saved_registers.r1,  ARM_V7M_R1);
    qemu_set_register(g_saved_registers.r2,  ARM_V7M_R2);
    qemu_set_register(g_saved_registers.r3,  ARM_V7M_R3);
    qemu_set_register(g_saved_registers.r4,  ARM_V7M_R4);
    qemu_set_register(g_saved_registers.r5,  ARM_V7M_R5);
    qemu_set_register(g_saved_registers.r6,  ARM_V7M_R6);
    qemu_set_register(g_saved_registers.r7,  ARM_V7M_R7);
    qemu_set_register(g_saved_registers.r8,  ARM_V7M_R8);
    qemu_set_register(g_saved_registers.r9,  ARM_V7M_R9);
    qemu_set_register(g_saved_registers.r10, ARM_V7M_R10);
    qemu_set_register(g_saved_registers.r11, ARM_V7M_R11);
    qemu_set_register(g_saved_registers.r12, ARM_V7M_R12);
    qemu_set_register(g_saved_registers.sp,  ARM_V7M_SP);
    qemu_set_register(g_saved_registers.lr,  ARM_V7M_LR);
    qemu_set_register(g_saved_registers.pc,  ARM_V7M_PC);
    return true;
}

// static bool disable_timer_interrupt(uint32_t timer_irq_num) {
//     // Disable the timer interrupt in the NVIC
//     uint32_t nvic_icer_base = 0xe000e180; // NVIC_ICER base address
//     uint32_t reg_offset = (timer_irq_num / 32) * 4;
//     uint32_t bit_mask = 1U << (timer_irq_num % 32);
//     uint32_t icer_addr = nvic_icer_base + reg_offset;

//     uint32_t current_value = 0;
//     qemu_plugin_read_memory(icer_addr, (uint8_t*)&current_value, sizeof(uint32_t));
//     current_value |= bit_mask;
//     qemu_plugin_write_memory(icer_addr, (uint8_t*)&current_value, sizeof(uint32_t));

//     return true;
// }

static char g_fuzzing_buf[1024];
static char g_fuzzing_input[1024];
static bool first_visit_to_anchor = true;

void anchor(unsigned int cpu_index, void *udata)
{
    // Currently reserving 0xDEADBEEF for a crash, we should have a better systems
    // For example, all exceptions?
    if (!coverage) {
        utils_die("Coverage not enabled, cannot assert coverage data");
    }
    if (!udata) return;

    if (first_visit_to_anchor) {
        printf("[Fuzz] Reached first anchor point. Starting fuzzing...\n");
        // disable_timer_interrupt(66); // disable timer interrupt (IRQ 66)
        save_registers();
        first_visit_to_anchor = false;
    }

    const char *input_str = (const char *)udata;
	//TODO: Fix this buffer thing
    strncpy(g_fuzzing_buf, input_str, sizeof(g_fuzzing_buf) - 1);
    g_fuzzing_buf[sizeof(g_fuzzing_buf) - 1] = '\0';

    // Split into filename and numbers
    char *anchor_id = strtok(g_fuzzing_buf, ":");
    if (anchor_id == NULL) {
        utils_die("Couldn't get the anchor id from string");
    }
    char *numbers = strtok(NULL, ":");
    if (numbers == NULL) {
        utils_die("Couldn't get the target registers/memory from string");
    }

    // finish last anchor
    if (last_anchor_id != -1) {
        fuzz_finish(last_anchor_id);
    }
    last_anchor_id = strtoul(anchor_id, NULL, 0);

    uint32_t read_count = 0;
    // uint8_t length = 0;
    // uint32_t msgid = 0;
    // uint8_t crc_extra = 0;
    // const uint8_t *payload = NULL;
    // uint8_t real_length = 0;
    // bool validation_mode = false;
    // //validation mode
    // if (validation_mode) {
    //     // use fixed input for validation
    //     length = 25;
    //     real_length = 25;
    //     msgid = 126; // GPS_RAW_INT
    //     crc_extra = 220; // GPS_RAW_INT
    //     static const uint8_t fixed_payload[25] = {0x00, 0x9C, 0xA8, 0x9D, 0x81, 0xEB, 0x66, 0x66, 0xDF, 0x66, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x6B, 0x1D};
    //     payload = fixed_payload;
    //     goto end;
    // }
fuzz:
    while (true) {
        read_count = fuzz_buffer_read(last_anchor_id, g_fuzzing_input, sizeof(g_fuzzing_input));
        if (read_count > 15 && read_count < 262) {
            break;
        }
        fuzz_finish(last_anchor_id);
    }
    // one byte length
    uint8_t length = g_fuzzing_input[0];
    // 24-bits to msgid
    uint32_t msgid =
        ((uint32_t)g_fuzzing_input[1] & 0xFF) << 16 |
        ((uint32_t)g_fuzzing_input[2] & 0xFF) << 8  |
        ((uint32_t)g_fuzzing_input[3] & 0xFF);
    // get crc extra from mavlink message entry
    const mavlink_msg_entry_t *mavlink_msg_entry = mavlink_get_msg_entry(msgid);
    if (mavlink_msg_entry == NULL || msgid == 126 || msgid == 32) {
        // fprintf(stderr, "Unknown MAVLink message ID: %u\n", msgid);
        fuzz_finish(last_anchor_id);
        goto fuzz;
    }

    uint8_t crc_extra = mavlink_msg_entry->crc_extra;

    uint8_t sys_id = g_fuzzing_input[4];
    uint8_t comp_id = g_fuzzing_input[5];

    if (msgid == 75 && sys_id == 126 && comp_id == 42) {
        fuzz_finish(last_anchor_id);
        goto fuzz;
    }
    // payload is rest of data
    const uint8_t *payload = (const uint8_t *)&g_fuzzing_input[6];
    // real length is total - 6 bytes
    uint8_t real_length = read_count - 6;
// end:
    assign_fuzzed_input(msgid, crc_extra, payload, length, real_length, sys_id, comp_id);
    create_fuzzed_mavlink_packet(msgid, crc_extra, payload, length, real_length, sys_id, comp_id);
    restore_registers();
}
