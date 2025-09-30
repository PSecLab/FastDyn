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

// Storage
static volatile char * storage_memory = NULL;
static const size_t storage_size = 32 * 1024; // 32KB of simulated storage

/**
 * @brief Read a block of data from simulated storage
 *
 * Must be called like this from virtuals.txt:
 *
 * <address/symbol> storage_read_block *
 */
void storage_read_block(unsigned int cpu_index, void *udata) {
    uint8_t * temp_buffer = NULL;

    if (storage_memory == NULL) {
        storage_memory = malloc(storage_size);
        if (storage_memory == NULL) {
            fprintf(stderr, "Failed to allocate storage memory\n");
            goto end;
        }
        memset((void*)storage_memory, 0xFF, storage_size); // Initialize to 0xFF
        // Load EEPROM header
        // First four the magic[2] = PA, revision=6, and spare=0
        storage_memory[0] = 0x50;
        storage_memory[1] = 0x41;
        storage_memory[2] = 0x06;
        storage_memory[3] = 0x00;
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

/**
 * @brief Write block to simulated storage
 *
 * This function simulates writing a block of data to non-volatile storage.
 * It uses registers R1, R2, and R3 to get the destination address,
 * offset, and size respectively. The data is written to an internal buffer
 * that simulates the storage device.
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> storage_write_block *
 */
void storage_write_block(unsigned int cpu_index, void *udata) {
    uint8_t * temp_buffer = NULL;

    if (storage_memory == NULL) {
        storage_memory = malloc(storage_size);
        if (storage_memory == NULL) {
            fprintf(stderr, "Failed to allocate storage memory\n");
            goto end;
        }
        memset((void*)storage_memory, 0xFF, storage_size); // Initialize to 0xFF
        // Load EEPROM header
        // First four the magic[2] = PA, revision=6, and spare=0
        storage_memory[0] = 0x50;
        storage_memory[1] = 0x41;
        storage_memory[2] = 0x06;
        storage_memory[3] = 0x00;
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

// Timer functions
/**
 * @brief Get high-resolution time in microseconds
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> hrt_micros64 *
 */
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

// Voltage
/**
 * @brief Simulate ADC voltage reading
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> adc_voltage *
 */
void adc_voltage(unsigned int cpu_index, void *udata) {
    // Return a fixed voltage of 5.0V for testing
    FloatConverter voltage_conv;
    voltage_conv.f = 5.0f;
    uint32_t voltage_int = voltage_conv.i;

    qemu_set_register(voltage_int, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

// volatile static const int RC_SAFETY_ARMED = 1;
// volatile static const int RC_SAFETY_DISARMED = 0;

// Servo / RC Channel
/**
 * @brief Write PWM value to a servo channel
 *
 * This function retrieves the channel number and PWM value from
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> write_channel
 */
void write_channel(unsigned int cpu_index, void *udata)
{
    uint8_t chan = (uint8_t)qemu_get_register(ARM_V7M_R1);
    uint16_t pwm = (uint16_t)qemu_get_register(ARM_V7M_R2);
    if (!set_servo_pwm(chan, pwm))
    {
        printf("Failed to set servo PWM: Channel=%d, PWM=%d\n", chan, pwm);
    }
}

// Wheel Encoder
static const int encoder_counts_per_rev = 3200;
static const float wheel_radius = 0.069f; // meters

/**
 * @brief Initialize wheel encoder parameters
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> init_wheel_encoder
 */
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

/**
 * @brief Copy wheel encoder state to frontend
 *
 * This function reads the current position of the wheel encoders from Gazebo
 * and updates the corresponding fields in the ArduRover simulation.
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> copy_wheel_encoder_state_to_frontend
 */
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

// GPS
/**
 * Set GPS type to MAVLink
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> gps_get_type_mavlink
 */
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

// Compass
typedef struct {
    float mag_x_gauss;
    float mag_y_gauss;
    float mag_z_gauss;
} SimulatorMagnetometer;

typedef struct {
    int16_t mag_x_raw;
    int16_t mag_y_raw;
    int16_t mag_z_raw;
} HMC5843RawData;

// Clamp to signed 12-bit range [-2048, 2047]
static inline int16_t constrain_int16(int value, int min_val, int max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return (int16_t)value;
}

// Convert simulator data to HMC5843 raw format
HMC5843RawData convert_to_hmc5843(SimulatorMagnetometer sim_data) {
    const float lsb_per_gauss = 1090.0f;

    int temp_x = (int)(sim_data.mag_x_gauss * lsb_per_gauss);
    int temp_y = (int)(sim_data.mag_y_gauss * lsb_per_gauss);
    int temp_z = (int)(sim_data.mag_z_gauss * lsb_per_gauss);

    // Apply remapping and sign adjustments
    int mag_x_raw = -temp_x;   // raw X
    int mag_y_raw = temp_z;    // raw Z -> Y
    int mag_z_raw = -temp_y;   // raw Y -> Z (negated)

    // Clamp to ±2048
    mag_x_raw = constrain_int16(mag_x_raw, -2048, 2047);
    mag_y_raw = constrain_int16(mag_y_raw, -2048, 2047);
    mag_z_raw = constrain_int16(mag_z_raw, -2048, 2047);

    HMC5843RawData raw = {
        .mag_x_raw = (int16_t)mag_x_raw,
        .mag_y_raw = (int16_t)mag_y_raw,
        .mag_z_raw = (int16_t)mag_z_raw
    };

    return raw;
}

// Pack into HMC5843 wire format (big-endian order X, Z, Y)
void pack_to_hmc5843_wire_format(HMC5843RawData raw, uint8_t out_bytes[6]) {
    // X
    out_bytes[0] = (uint8_t)((raw.mag_x_raw >> 8) & 0xFF);
    out_bytes[1] = (uint8_t)(raw.mag_x_raw & 0xFF);
    // Z
    out_bytes[2] = (uint8_t)((raw.mag_z_raw >> 8) & 0xFF);
    out_bytes[3] = (uint8_t)(raw.mag_z_raw & 0xFF);
    // Y
    out_bytes[4] = (uint8_t)((raw.mag_y_raw >> 8) & 0xFF);
    out_bytes[5] = (uint8_t)(raw.mag_y_raw & 0xFF);
}

typedef struct {
    const float diagonals[3];    // Scale factors for X, Y, Z axes
    const float offdiagonals[3]; // Cross-axis correction factors
    const float offset[3];       // Hard iron offsets for X, Y, Z axes
} magnetometer_calibration_t;

magnetometer_calibration_t mag_cal = {
    .diagonals = {1.0f, 1.0f, 1.0f},
    .offdiagonals = {0.0f, 0.0f, 0.0f},
    .offset = {-0.0012279493f, 1.3877788e-17f, 0.47694647f}
};

/**
 * Calibration no-op to write valid data and skip calibration
 *
 * Must be called like this from virtuals.txt
 *
 * <address/symbol> compass_calibrate *
 */
void compass_calibrate(unsigned int cpu_index, void *udata) {
    uint32_t this_pointer = (uint32_t)qemu_get_register(ARM_V7M_R0);
    uint32_t offset = this_pointer + 0x2c; // offset to mag_cal field
    float scaling[3] = {1.0f, 1.0f, 1.0f};
    qemu_plugin_write_memory(offset, (uint8_t *)scaling, sizeof(scaling));

    // return with 0x01 (success)
    qemu_set_register(1, ARM_V7M_R0);
    qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
}

/**
 * Must be called like this from virtuals.txt
 *
 * <address/symbol> compass_read_block *
 */
void compass_read_block(unsigned int cpu_index, void *udata) {
    uint32_t reg = (uint32_t)qemu_get_register(ARM_V7M_R1);
    uint32_t buf = (uint32_t)qemu_get_register(ARM_V7M_R2);
    uint32_t size = (uint32_t)qemu_get_register(ARM_V7M_R3);
    (void)size; // unused

    if (reg != 0x03) {
        printf("Compass read block unknown register: 0x%X\n", reg);
        qemu_set_register(0, ARM_V7M_R0); // failure
        qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
        return;
    }

    double mag_x = 0.0;
    double mag_y = 0.0;
    double mag_z = 0.0;
    if (!get_mag_reading(&mag_x, &mag_y, &mag_z)) {
        printf("Failed to get magnetometer reading from Gazebo\n");
        qemu_set_register(0, ARM_V7M_R0); // failure
        qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
        return;
    }

    SimulatorMagnetometer sim_data = {
        .mag_x_gauss = (float)mag_x,
        .mag_y_gauss = (float)mag_y,
        .mag_z_gauss = (float)mag_z
    };

    // convert to raw format
    HMC5843RawData raw_data = convert_to_hmc5843(sim_data);
    uint8_t wire_bytes[6];
    pack_to_hmc5843_wire_format(raw_data, wire_bytes);
    qemu_plugin_write_memory(buf, wire_bytes, 6);

    // return with 0x01 (success)
    qemu_set_register(1, ARM_V7M_R0);
    qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
}

/**
 * @brief Advancing the time in the tick handler
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> advance_time_in_tick_handler *
 */
void chibiOS_tick_handler(unsigned int cpu_index, void *udata) {
    uint32_t tick_frequency = 1000; // 1 kHz

    int64_t current_nanos = qemu_plugin_get_virtual_timer();
    uint32_t current_millis = (uint32_t)(current_nanos / 1000000);

    uint32_t system_ticks = current_millis * tick_frequency / 1000;
    qemu_set_register(system_ticks, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

uint32_t gcs_uarts[8] = {0};

/**
 * @brief Record UART used by GCS
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> create_gcs_mavlink_backend
 */
void create_gcs_mavlink_backend(unsigned int cpu_index, void *udata) {
    uint32_t uart_num = (uint32_t)qemu_get_register(ARM_V7M_R2);
    // find the first empty slot and put it in
    for (int i = 0; i < 8; i++) {
        if (gcs_uarts[i] == 0) {
            gcs_uarts[i] = uart_num;
            printf("GCS using UART at address 0x%X\n", uart_num);
            break;
        }
    }
}

/**
 * @brief Record every "send text" call from inside firmware
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> gcs_send_text
 */
void gcs_send_text(unsigned int cpu_index, void *udata) {
    uint32_t memory_location = (uint32_t)qemu_get_register(ARM_V7M_R2);

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));

    while (1) {
        uint8_t ch;
        qemu_plugin_read_memory(memory_location, &ch, 1);
        if (ch == 0 || (memory_location - (uint32_t)qemu_get_register(ARM_V7M_R2)) >= 255) {
            break;
        }
        buffer[memory_location - (uint32_t)qemu_get_register(ARM_V7M_R2)] = (char)ch;
        memory_location++;
    }

    buffer[255] = 0; // ensure null termination

    printf("GCS Text: %s\n", buffer);
}

/**
 * @brief Only send the banner once over mavlink
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> gcs_send_banner_once *
 */
void gcs_send_banner_once(unsigned int cpu_index, void *udata) {
    static int banner_sent = 0;
    if (!banner_sent) {
        return;
    }

    // Set banner_sent to true
    banner_sent = 1;

    // return from function and return 0
    qemu_set_register(0, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}
