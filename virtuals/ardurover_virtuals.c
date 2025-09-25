/**
 * @brief Reusable virtuals for ArduRover
 *
 * This file implements the common virtual functions used by ArduRover.
 *
 * @file FastDyn/virtuals/ardurover_virtuals.c
 * @author Michael Rooney
 */

#include "virtuals.h"
#include "gazebo_wrapper.h"
#include <math.h>

// volatile static const int RC_SAFETY_ARMED = 1;
// volatile static const int RC_SAFETY_DISARMED = 0;

void write_channel(unsigned int cpu_index, void *udata)
{
    uint8_t chan = (uint8_t)qemu_get_register(ARM_V7M_R1);
    uint16_t pwm = (uint16_t)qemu_get_register(ARM_V7M_R2);
    if (!set_servo_pwm(chan, pwm))
    {
        printf("Failed to set servo PWM: Channel=%d, PWM=%d\n", chan, pwm);
    }
}

static const int encoder_counts_per_rev = 3200;
static const float wheel_radius = 0.069f; // meters

void init_wheel_encoder(unsigned int cpu_index, void *udata)
{
    // Set proper wheel radius for both wheels
    uint32_t this_pointer = (uint32_t)qemu_get_register(ARM_V7M_R0);
    qemu_plugin_write_memory(this_pointer + 0x18, (uint8_t *)&wheel_radius, sizeof(int));
    qemu_plugin_write_memory(this_pointer + 0x1C, (uint8_t *)&wheel_radius, sizeof(float));

    // Set proper wheel type for both wheels
    uint8_t type = 1;
    qemu_plugin_write_memory(this_pointer, (uint8_t *)&type, sizeof(uint8_t));
    qemu_plugin_write_memory(this_pointer + 1, (uint8_t *)&type, sizeof(uint8_t));
}

void copy_wheel_encoder_state_to_frontend(unsigned int cpu_index, void *udata)
{
    uint32_t instance_number_offset = (uint32_t)qemu_get_register(ARM_V7M_R0) + 0x8;
    uint32_t instance_number = 0;
    qemu_plugin_read_memory(instance_number_offset, (uint8_t *)&instance_number, sizeof(uint32_t));

    double motor0_pos = 0.0;
    double motor2_pos = 0.0;
    if (!get_joint_state(&motor0_pos, &motor2_pos)) {
        printf("Failed to get joint state from Gazebo\n");
        return;
    }

    int64_t current_nanos = qemu_plugin_get_virtual_timer();
    uint32_t current_millis = (uint32_t)(current_nanos / 1000000);

    int32_t distance_count = 0;
    if (instance_number == 0) {
        distance_count = (int32_t)(motor0_pos * encoder_counts_per_rev / (2.0 * M_PI));
    } else if (instance_number == 1) {
        distance_count = (int32_t)(motor2_pos * encoder_counts_per_rev / (2.0 * M_PI));
    } else {
        printf("Unknown wheel encoder instance number: %u\n", instance_number);
        return;
    }

    uint32_t total_abs_count = 0;
    if (distance_count < 0) {
        total_abs_count = (uint32_t)(-distance_count);
    } else {
        total_abs_count = (uint32_t)distance_count;
    }

    qemu_set_register(distance_count, ARM_V7M_R1);
    qemu_set_register(total_abs_count, ARM_V7M_R2);
    qemu_set_register(0, ARM_V7M_R3); // total error count is 0 in simulation
    qemu_plugin_write_memory((uint32_t)qemu_get_register(ARM_V7M_SP) + 0x4,
                             (uint8_t *)&current_millis,
                             sizeof(uint32_t));
}

void gps_get_type_mavlink(unsigned int cpu_index, void *udata)
{
    uint8_t gps_type = 6; // Default to GPS_TYPE_MAVLINK
    qemu_set_register(gps_type, ARM_V7M_R6);
}

/**
 * INS virtual functions
 *
 * The data is packed in the following format:
    Bytes 0-1:   Accel Y (16-bit, big-endian)
    Bytes 2-3:   Accel X (16-bit, big-endian)
    Bytes 4-5:   Accel Z (16-bit, big-endian)
    Bytes 6-7:   Temperature (16-bit, big-endian)
    Bytes 8-9:   Gyro Y (16-bit, big-endian)
    Bytes 10-11: Gyro X (16-bit, big-endian)
    Bytes 12-13: Gyro Z (16-bit, big-endian)

    The two register values can mean either (0x72) counting the bytes to read or
    (0x74) writing the actual data.
 */
void fifo_count_to_bytes(uint16_t count, uint8_t out[2]) {
    out[0] = (count >> 8) & 0xFF; // high byte
    out[1] = count & 0xFF;        // low byte
}

void convert_int16_array_to_be_bytes(const int16_t* input, size_t length, uint8_t* output) {
    for (size_t i = 0; i < length; ++i) {
        int16_t value = input[i];

        // Convert to big-endian
        output[2 * i]     = (uint8_t)(value >> 8);      // high byte
        output[2 * i + 1] = (uint8_t)(value & 0xFF);     // low byte
    }
}

#define ACCEL_LSB_PER_G     2048.0f
#define GRAVITY             9.80665f
#define GYRO_LSB_PER_DPS    16.4f
#define RAD_TO_DEG          (180.0f / 3.14159265f)
#define TEMP_LSB_PER_C      340.0f
#define TEMP_ZERO_C         21.0f
#define TEMP_ZERO_C_INV     25.0f

void convert_to_invensense(const float in[7], int16_t out[7]) {
    // Convert each field using Invensense scaling, as in Python
    int16_t accel_x = (int16_t)(in[0] / GRAVITY * ACCEL_LSB_PER_G);
    int16_t accel_y = (int16_t)(in[1] / GRAVITY * ACCEL_LSB_PER_G);
    int16_t accel_z = (int16_t)(in[2] / GRAVITY * ACCEL_LSB_PER_G);

    int16_t temp = (int16_t)((in[3] - TEMP_ZERO_C) * TEMP_LSB_PER_C);

    int16_t gyro_x = (int16_t)(in[4] * RAD_TO_DEG * GYRO_LSB_PER_DPS);
    int16_t gyro_y = (int16_t)(in[5] * RAD_TO_DEG * GYRO_LSB_PER_DPS);
    int16_t gyro_z = (int16_t)(in[6] * RAD_TO_DEG * GYRO_LSB_PER_DPS);

    // Cast signed int16_t to unsigned uint16_t for output buffer
    out[0] = accel_x;
    out[1] = accel_y;
    out[2] = accel_z;
    out[3] = temp;
    out[4] = gyro_x;
    out[5] = gyro_y;
    out[6] = gyro_z;
}

/**
 * Must be called like this from virtuals.txt
 *
 * <address/symbol> ins_block_read *
 */
void ins_block_read(unsigned int cpu_index, void *udata) {
    uint32_t reg = (uint32_t)qemu_get_register(ARM_V7M_R1);
    uint32_t buf = (uint32_t)qemu_get_register(ARM_V7M_R2);
    uint32_t size = (uint32_t)qemu_get_register(ARM_V7M_R3);

    if (reg == 0x72) {
        uint8_t count_bytes[2];
        fifo_count_to_bytes(14, count_bytes);
        qemu_plugin_write_memory(buf, count_bytes, 2);
        qemu_set_register(1, ARM_V7M_R0); // success
        qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
    }
    else if (reg == 0x74) {
        if (size < 14) {
            printf("INS block read size too small: %u\n", size);
            qemu_set_register(0, ARM_V7M_R0); // failure
            qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
            return;
        }

        sitl_state_data_t state;
        int success = get_latest_sitl_state(&state);
        if (!success) {
            printf("Failed to get latest SITL state for INS block read\n");
            qemu_set_register(0, ARM_V7M_R0); // failure
            qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
            return;
        }

        imu_t imu = state.imu;
        float accel_x = imu.accel_body.x;
        float accel_y = imu.accel_body.y;
        float accel_z = imu.accel_body.z;
        float gyro_x = imu.gyro.x;
        float gyro_y = imu.gyro.y;
        float gyro_z = imu.gyro.z;
        float temp_celsius = TEMP_ZERO_C_INV;

        float imu_data[7] = {
            accel_x,
            accel_y,
            accel_z,
            temp_celsius,
            gyro_x,
            gyro_y,
            gyro_z
        };

        int16_t imu_data_int16[7];
        convert_to_invensense(imu_data, imu_data_int16);

        uint8_t imu_data_bytes[14];
        convert_int16_array_to_be_bytes(imu_data_int16, 7, imu_data_bytes);
        qemu_plugin_write_memory(buf, imu_data_bytes, 14);
        qemu_set_register(1, ARM_V7M_R0); // success
        qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
    } else {
        printf("INS block read unknown register: 0x%X\n", reg);
        qemu_set_register(0, ARM_V7M_R0); // failure
        qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
    }
}


