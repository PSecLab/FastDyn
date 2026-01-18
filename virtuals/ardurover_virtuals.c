/**
 * @brief Reusable virtuals for ArduRover
 *
 * This file implements the common virtual functions used by ArduRover.
 *
 * @file FastDyn/virtuals/ardurover_virtuals.c
 * @author Michael Rooney
 */

#include <fcntl.h>
#include <unistd.h>
#include "virtuals.h"
#include "gazebo_wrapper.h"
#include <math.h>
#include "mavlink_lib.h"
#include <arpa/inet.h>
#include <stdatomic.h>

#define PROFILE_INS_READS 0
#define PROFILE_COMPASS_READS 1

// Global to track last sim time from QEMU
_Atomic int64_t last_sim_time_ns = 0;

static void catch_up(void *opaque)
{
    (void) opaque;

    int64_t last_seen = -1;

    while (1) {
        int64_t sim_ns = atomic_load(&last_sim_time_ns);

        if (sim_ns != last_seen) {
            double target_time_s = (double)sim_ns / 1e9;
            if (!advance_simulation(target_time_s)) {
                fprintf(stderr, "Failed to advance simulation to %.6f s\n", target_time_s);
            }
            last_seen = sim_ns;
        }

        usleep(1000);
    }
}

/**
 * @brief Begins the periodic timer for advancing the simulation
 *        given the QEMU sim time
 *
 * Called like this from virtuals.txt:
 *
 * <entry point> start_advancing_sim
 */
void start_advancing_sim(unsigned int cpu_index, void *udata) {
    (void) cpu_index;
    (void) udata;
    // TODO: Make this catch-up function it's own thread instead of using a timer
    // that way it can run at a higher frequency if needed and won't cause missed
    // timer events.
    // qemu_plugin_timer_new_period_ns(catch_up, NULL, 1e8);

    // start a thread to advance the sim
    pthread_t advance_sim_thread;
    if (pthread_create(&advance_sim_thread, NULL, (void *(*)(void *))catch_up, NULL) != 0) {
        fprintf(stderr, "Failed to create advance simulation thread\n");
    } else {
        pthread_detach(advance_sim_thread);
    }
}

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

    // printf("storage_read_block: offset=0x%08x, address=0x%08x, size=%lu\n", loc, dst, size);

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
    // int seconds = (int)(micros / 1000000);
    // printf("hrt_micros64: %llu microseconds\n", (unsigned long long)micros);
    // printf("  (approx %d seconds)\n", seconds);

    qemu_set_register(micros_upper_32, ARM_V7M_R1);
    qemu_set_register(micros_lower_32, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

/**
 * @brief Micros function that returns uint32_t value and is MMIO
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> micros32 *
 */
void micros32(unsigned int cpu_index, void *udata) {
    int64_t nanos = qemu_plugin_get_virtual_timer();

    if (nanos < 0) {
        fprintf(stderr, "Error getting virtual timer\n");
        return;
    }

    uint32_t micros = (uint32_t)((nanos / 1000) & 0xFFFFFFFF);
    // int seconds = (int)(micros / 1000000);
    // printf("micros32: %u microseconds\n", micros);
    // printf("  (approx %d seconds)\n", seconds);

    qemu_set_register(micros, ARM_V7M_R0);
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
        fprintf(stderr, "Failed to set servo PWM: Channel=%d, PWM=%d\n", chan, pwm);
    }
    // if (pwm != 0 && pwm != 1500)
    // {
    //     // Uncomment for debugging non-center PWM values
    //     printf("write_channel: Channel=%d, PWM=%d\n", chan, pwm);
    // }
}

// Wheel Encoder
static const int encoder_counts_per_rev = 3200;
static const float wheel_radius = 0.069f; // meters

/**
 * @brief Initialize wheel encoder parameters
 *
 * @requirements.init_wheel_encoder
 *
 * AP_WheelEncoder:
 *   _type:
 *     offset: 0x0
 *     size: 0x2
 *   _wheel_radius:
 *     offset: 0x8
 *     size: 0x8
 *
 * @end_requirements
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> init_wheel_encoder
 */
void init_wheel_encoder(unsigned int cpu_index, void *udata)
{
    const char *type_str = (const char *)udata;
    unsigned long type = strtoul(type_str, NULL, 0);
    uint8_t type_u8 = (uint8_t)type;

    uint32_t this_pointer = (uint32_t)qemu_get_register(ARM_V7M_R0);

    if (type_u8 != 1 && type_u8 != 0) {
        fprintf(stderr, "Warning: Undefined beavhior. You must set type to either NONE (0) or QUADRATURE (1).\n");
    } else if (type_u8 == 1) {
        // Set proper wheel radius for both wheels
        uint32_t this_pointer = (uint32_t)qemu_get_register(ARM_V7M_R0);
        qemu_plugin_write_memory(this_pointer + 0x8, (uint8_t *)&wheel_radius, sizeof(float));
        qemu_plugin_write_memory(this_pointer + 0xC, (uint8_t *)&wheel_radius, sizeof(float));
    }

    // Set proper wheel type for both wheels
    qemu_plugin_write_memory(this_pointer, (uint8_t *)&type_u8, sizeof(uint8_t));
    qemu_plugin_write_memory(this_pointer + 1, (uint8_t *)&type_u8, sizeof(uint8_t));
}

/**
 * @brief Copy wheel encoder state to frontend
 *
 * This function reads the current position of the wheel encoders from Gazebo
 * and updates the corresponding fields in the ArduRover simulation.
 *
 * @requirements.copy_wheel_encoder_state_to_frontend
 *
 * AP_WheelEncoder_Backend:
 *   _frontend:
 *     offset: 0x4
 *     size: 0x4
 * AP_WheelEncoder:
 *   drivers:
 *     offset: 0x6c
 *     size: 0x8
 *
 * @end_requirements
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> copy_wheel_encoder_state_to_frontend
 */
void copy_wheel_encoder_state_to_frontend(unsigned int cpu_index, void *udata)
{

    uint32_t driver_address = (uint32_t)qemu_get_register(ARM_V7M_R0);
    uint32_t frontend_address = 0;
    qemu_plugin_read_memory(driver_address + 0x4, (uint8_t *)&frontend_address, sizeof(uint32_t));
    uint32_t driver1_address = 0;
    uint32_t driver2_address = 0;
    qemu_plugin_read_memory(frontend_address + 0x6c, (uint8_t *)&driver1_address, sizeof(uint32_t));
    qemu_plugin_read_memory(frontend_address + 0x70, (uint8_t *)&driver2_address, sizeof(uint32_t));

    double motor0_pos = 0.0;
    double motor2_pos = 0.0;
    if (!get_joint_state(&motor0_pos, &motor2_pos)) {
        fprintf(stderr, "Failed to get joint state from Gazebo\n");
        return;
    }

    int64_t current_nanos = qemu_plugin_get_virtual_timer();
    uint32_t current_millis = (uint32_t)(current_nanos / 1000000);

    // TODO: Take out hard coding of addresses
    int32_t distance_count = 0;
    if (driver_address == driver1_address) { // 0 index
        distance_count = (int32_t)(motor0_pos * encoder_counts_per_rev / (2.0 * M_PI));
    } else if (driver_address == driver2_address) { // 1 index
        distance_count = (int32_t)(motor2_pos * encoder_counts_per_rev / (2.0 * M_PI));
    } else {
        fprintf(stderr, "Unknown wheel encoder driver: %x\n", driver_address);
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
 * @brief Send GPS MAVLink message periodically
 *
 * This function is called periodically to send a GPS MAVLink message
 * with simulated GPS data.
 *
 * @param opaque message not used
 */
static void send_gps_mavlink_message(void *opaque)
{
    (void) opaque;

    gps_data_t gps_data;
    if (!get_navsat_reading(&gps_data)) {
        fprintf(stderr, "Failed to get GPS reading from Gazebo\n");
        return;
    }

    gps_input_t gps_message = {
        .latitude_deg = gps_data.lat,
        .longitude_deg = gps_data.lon,
        .altitude_m = gps_data.alt,
        .velocity_n = (float)gps_data.vel_n,
        .velocity_e = (float)gps_data.vel_e,
        .velocity_d = (float)gps_data.vel_d,
        .timestamp_sec = (uint32_t)gps_data.sec,
        .timestamp_nsec = gps_data.nsec,
        .fix_type = 3, // 3D fix
        .satellites_visible = 10, // Arbitrary number of satellites
        .yaw_deg = gps_data.yaw_deg
    };

    // 220 https://mavlink.io/en/messages/common.html#MAV_COMP_ID_GPS
    send_mavlink_gps_input(1, 220, &gps_message);
}

static void gps_thread_func(void *arg)
{
    (void)arg;
    const int interval_ms = 100; // 100 ms interval
    while (1) {
        send_gps_mavlink_message(NULL);
        usleep(interval_ms * 1000);
    }
}


/**
 * Set GPS type to MAVLink
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> gps_get_type_mavlink
 */
void gps_get_type_mavlink(unsigned int cpu_index, void *udata)
{
    static bool requested_timer = false;
    // const char *msg = "Hello from GPS MAVLink!";
    uint8_t gps_type = 14; // Default to GPS_TYPE_MAVLINK
    qemu_set_register(gps_type, ARM_V7M_R6);
    // printf("gps_get_type_mavlink: returning GPS type %u\n", gps_type);
    if (!requested_timer) {
        requested_timer = true;
        // // Request periodic timer every 100ms
        // qemu_plugin_timer_new_period_ns(send_gps_mavlink_message, (void *)msg, 1e8);
        // Start GPS thread
        pthread_t gps_thread;
        if (pthread_create(&gps_thread, NULL, (void *(*)(void *))gps_thread_func, NULL) != 0) {
            fprintf(stderr, "Failed to create GPS thread\n");
        } else {
            pthread_detach(gps_thread);
        }
    }
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

    // noise directly to LSBs

    out[0] += (rand() % 5) - 2;  // ±2 LSB accel
    out[1] += (rand() % 5) - 2;
    out[2] += (rand() % 5) - 2;

    out[4] += (rand() % 7) - 3;  // ±3 LSB
    out[5] += (rand() % 7) - 3;
    out[6] += (rand() % 7) - 3;
}

#if PROFILE_INS_READS == 1
// total time
static volatile double ins_total_elapsed_time_s = 0.0;
static volatile double ins_last_time_s = 0.0;
static volatile int ins_read_count = 0;
#endif // PROFILE_INS_READS

/**
 * Must be called like this from virtuals.txt
 *
 * <address/symbol> ins_block_read *
 */
void ins_block_read(unsigned int cpu_index, void *udata) {
    uint32_t reg = (uint32_t)qemu_get_register(ARM_V7M_R1);
    uint32_t buf = (uint32_t)qemu_get_register(ARM_V7M_R2);
    uint32_t size = (uint32_t)qemu_get_register(ARM_V7M_R3);
    uint16_t count = 14 * 24; // 24 samples of 14 bytes each (3 x 8 samples)

    if (reg == 0x72) {
        uint8_t count_bytes[2];
        fifo_count_to_bytes(count, count_bytes);
        qemu_plugin_write_memory(buf, count_bytes, 2);
        qemu_set_register(1, ARM_V7M_R0); // success
        qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
    }
    else if (reg == 0x74) {
        // instrumentation to check frequency of reads of IMU
#if PROFILE_INS_READS == 1
        if (ins_read_count == 0) {
            ins_last_time_s = (double)qemu_plugin_get_virtual_timer() / 1e9;
        } else {
            double current_time_s = (double)qemu_plugin_get_virtual_timer() / 1e9;
            double elapsed_s = current_time_s - ins_last_time_s;
            ins_total_elapsed_time_s += elapsed_s;
            ins_last_time_s = current_time_s;
            if (ins_read_count % 1000 == 0) {
                double average_interval_ms = (ins_total_elapsed_time_s / ins_read_count) * 1000.0;
                double frequency_hz = 1.0 / (average_interval_ms / 1000.0);
                printf("INS block read average interval: %.3f ms (%.2f Hz) over %d reads\n",
                       average_interval_ms, frequency_hz, ins_read_count);
            }
        }
        ins_read_count++;
#endif // PROFILE_INS_READS

        if (size < 14) {
            fprintf(stderr, "INS block read size too small: %u\n", size);
            qemu_set_register(0, ARM_V7M_R0); // failure
            qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
            return;
        }

        imu_batch_t imu_batch;
        int success = get_imu_batch(&imu_batch);
        if (!success) {
            fprintf(stderr, "Failed to get IMU batch for INS block read\n");
            qemu_set_register(0, ARM_V7M_R0); // failure
            qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
            return;
        }

        uint16_t mini_batch_size = 8 * 14; // 8 samples of 14 bytes each
        uint8_t imu_data_bytes[mini_batch_size];
        memset(imu_data_bytes, 0, sizeof(imu_data_bytes));
        for (int i = 0; i < 8; i++) {
            imu_t imu = imu_batch.imu[i];
            float accel_x = imu.accel_body.x;
            float accel_y = imu.accel_body.y;
            float accel_z = imu.accel_body.z;
            float gyro_x = imu.gyro.x;
            float gyro_y = imu.gyro.y;
            float gyro_z = imu.gyro.z;
            float temp_celsius = TEMP_ZERO_C_INV;

            // Apply remapping and sign adjustments here if needed

            float imu_data[7] = {
                accel_y,
                -1 * accel_x,
                accel_z,
                temp_celsius,
                gyro_y,
                -1 * gyro_x,
                gyro_z
            };

            int16_t imu_data_int16[7];
            convert_to_invensense(imu_data, imu_data_int16);

            uint8_t imu_sample[14];
            convert_int16_array_to_be_bytes(imu_data_int16, 7, imu_sample);
            memcpy(imu_data_bytes + (i * 14), imu_sample, 14);
        }

        // imu_t imu = imu_batch.imu[0];
        // float accel_x = imu.accel_body.x;
        // float accel_y = imu.accel_body.y;
        // float accel_z = imu.accel_body.z;
        // float gyro_x = imu.gyro.x;
        // float gyro_y = imu.gyro.y;
        // float gyro_z = imu.gyro.z;
        // float temp_celsius = TEMP_ZERO_C_INV;

        // float imu_data[7] = {
        //     accel_y,
        //     -1 * accel_x,
        //     accel_z,
        //     temp_celsius,
        //     gyro_y,
        //     -1 * gyro_x,
        //     gyro_z
        // };

        // int16_t imu_data_int16[7];
        // convert_to_invensense(imu_data, imu_data_int16);

        // uint8_t imu_data_bytes[14];
        // convert_int16_array_to_be_bytes(imu_data_int16, 7, imu_data_bytes);

        qemu_plugin_write_memory(buf, imu_data_bytes, mini_batch_size);
        qemu_set_register(1, ARM_V7M_R0); // success
        qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
    } else {
        fprintf(stderr, "INS block read unknown register: 0x%X\n", reg);
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
    // int mag_x_raw = -1 * temp_x;   // raw X
    // int mag_y_raw = temp_z;    // raw Z -> Y
    // int mag_z_raw = -1 * temp_y;   // raw Y -> Z (negated)

    int mag_x_raw = temp_x;
    int mag_y_raw = temp_y;
    int mag_z_raw = temp_z;

    // Clamp to ±2048
    mag_x_raw = constrain_int16(mag_x_raw, -2048, 2047);
    mag_y_raw = constrain_int16(mag_y_raw, -2048, 2047);
    mag_z_raw = constrain_int16(mag_z_raw, -2048, 2047);

    // y and z are negated because +z is up and +y is left from gazebo
    HMC5843RawData raw = {
        .mag_x_raw = (int16_t)(-1 * mag_x_raw),
        .mag_y_raw = (int16_t)(mag_y_raw),
        .mag_z_raw = (int16_t)(mag_z_raw)
        // .mag_y_raw = (int16_t)(mag_y_raw),
        // .mag_z_raw = (int16_t)(mag_z_raw)
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
    .offset = {-0.0012279493f, 1.3877788e-17f, 0.47694647f},
    .diagonals = {1.0f, 1.0f, 1.0f},
    .offdiagonals = {0.0f, 0.0f, 0.0f},
};

/**
 * @brief Calibration no-op to write valid data and skip calibration
 *
 * @requirements.compass_calibrate
 *
 * AP_Compass_HMC5843:
 *   _scaling:
 *     offset: 0x2c
 *     size: 0xc
 *
 * @end_requirements
 *
 * Must be called like this from virtuals.txt
 *
 * <address/symbol> compass_calibrate *
 */
void compass_calibrate(unsigned int cpu_index, void *udata) {
    uint32_t this_pointer = (uint32_t)qemu_get_register(ARM_V7M_R0);
    uint32_t offset = this_pointer + 0x2c; // offset to _scaling field
    float scaling[3] = {1.0f, 1.0f, 1.0f};
    qemu_plugin_write_memory(offset, (uint8_t *)scaling, sizeof(scaling));

    // return with 0x01 (success)
    qemu_set_register(1, ARM_V7M_R0);
    qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
}

// variable for profiling compass reads
// #if PROFILE_COMPASS_READS == 1
static volatile double compass_total_elapsed_time_s = 0.0;
static volatile double compass_last_time_s = 0.0;
static volatile int compass_read_count = 0;
// #endif // PROFILE_COMPASS_READS

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
        fprintf(stderr, "Compass read block unknown register: 0x%X\n", reg);
        qemu_set_register(0, ARM_V7M_R0); // failure
        qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
        return;
    }

    double mag_x = 0.0;
    double mag_y = 0.0;
    double mag_z = 0.0;
    if (!get_mag_reading(&mag_x, &mag_y, &mag_z)) {
        fprintf(stderr, "Failed to get magnetometer reading from Gazebo\n");
        qemu_set_register(0, ARM_V7M_R0); // failure
        qemu_set_register(qemu_get_register(ARM_V7M_LR), ARM_V7M_PC); // return
        return;
    }

// #if PROFILE_COMPASS_READS == 1
    if (compass_read_count == 0) {
        compass_last_time_s = (double)qemu_plugin_get_virtual_timer() / 1e9;
    } else {
        double current_time_s = (double)qemu_plugin_get_virtual_timer() / 1e9;
        double elapsed_s = current_time_s - compass_last_time_s;
        compass_total_elapsed_time_s += elapsed_s;
        compass_last_time_s = current_time_s;
        if (compass_read_count % 10 == 0) {
            double average_interval_ms = (compass_total_elapsed_time_s / compass_read_count) * 1000.0;
            double frequency_hz = 1.0 / (average_interval_ms / 1000.0);
            // printf("Compass read average interval: %.3f ms (%.2f Hz) over %d reads\n",
                //    average_interval_ms, frequency_hz, compass_read_count);
        }
    }
    compass_read_count++;
// #endif // PROFILE_COMPASS_READS

    // printf("Magnetometer reading (Gauss): X=%.3f, Y=%.3f, Z=%.3f\n", mag_x, mag_y, mag_z);
    // yaw
    // double yaw = atan2(mag_y, mag_x) * 180.0 / M_PI;
    // printf("Calculated yaw: %.3f degrees\n", yaw);

    // transformation of this data to HMC5843 format occurs in `convert_to_hmc5843`
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
 * @brief Ensuring accurate offsets for our compass backend
 *
 *
 * layout:
 *   <unknown>: 0x48 (magnetometer_calibration_t)
 *
 * TODO: Check removal.
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> compass_configure *
 */
void compass_configure(unsigned int cpu_index, void *udata) {
    uint32_t this_pointer = (uint32_t)qemu_get_register(ARM_V7M_R0);
    uint32_t offset = this_pointer + 0x48;
    qemu_plugin_write_memory(offset, (uint8_t *)&mag_cal, sizeof(magnetometer_calibration_t));

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
    // uint32_t tick_frequency = 1000; // 1 kHz
    uint32_t tick_frequency = 10000; // 1 MHz

    int64_t current_nanos = qemu_plugin_get_virtual_timer();
    uint32_t current_millis = (uint32_t)(current_nanos / 1000000);
    // printf("Current millis: %u\n", current_millis);
    uint32_t system_ticks = current_millis * tick_frequency / 1000;
    qemu_set_register(system_ticks, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

static uint32_t gcs_uarts[8] = {0};

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

    // printf("GCS Text: %s\n", buffer);
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
    if (banner_sent) {
        return;
    }

    // Set banner_sent to true
    banner_sent = 1;

    // return from function and return 0
    qemu_set_register(0, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

static RingBuffer ring_buffer;
static bool ring_buffer_initialized = false;

#define RING_BUFFER_SIZE 512

/**
 * @brief Read a byte from a UART used by GCS
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> gcs_read *
 */
void gcs_read(unsigned int cpu_index, void *udata) {
    if (!ring_buffer_initialized) {
        if (!ring_buffer_init(&ring_buffer, RING_BUFFER_SIZE)) {
            fprintf(stderr, "Failed to initialize GCS ring buffer\n");
            return;
        }
        ring_buffer_initialized = true;
    }

    uint32_t uart_num = (uint32_t)qemu_get_register(ARM_V7M_R0);
    int found = 0;

    for (int i = 0; i < 8; i++) {
        if (gcs_uarts[i] == uart_num) {
            found = 1;
            break;
        }
    }

    if (!found) {
        // fall through without returning
        return;
    }

    unsigned char byte = 0x00;
    read_byte(&ring_buffer, &byte);

    qemu_set_register((uint32_t)byte, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

/**
 * @brief Return the number of bytes available in the GCS ring buffer
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> gcs_bytes_available *
 */
void gcs_bytes_available(unsigned int cpu_index, void *udata) {
    if (!ring_buffer_initialized) {
        if (!ring_buffer_init(&ring_buffer, RING_BUFFER_SIZE)) {
            fprintf(stderr, "Failed to initialize GCS ring buffer\n");
            return;
        }
        ring_buffer_initialized = true;
    }

    uint32_t uart_num = (uint32_t)qemu_get_register(ARM_V7M_R0);
    int found = 0;

    for (int i = 0; i < 8; i++) {
        if (gcs_uarts[i] == uart_num) {
            found = 1;
            break;
        }
    }

    if (!found) {
        // fall through without returning
        return;
    }

    size_t available = bytes_available(&ring_buffer);

    qemu_set_register((uint32_t)available, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

static uint8_t sequence_number = 0;
// static struct sockaddr_in gcs_addr;
// static const char *gcs_ip = "127.0.0.1";
// static int gcs_sockfd = -1;
// static int gcs_addr_initialized = 0;

// int create_udp_client(int *out_sockfd,
//                       struct sockaddr_in *out_remote,
//                       const char *remote_ip) {
//     if (!out_sockfd || !out_remote || !remote_ip) return -1;

//     // 1. Create UDP socket
//     int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
//     if (sockfd < 0) {
//         perror("socket creation failed");
//         return -1;
//     }

//     // 2. Bind to local port 14552 so we have a fixed source port
//     struct sockaddr_in local_addr;
//     memset(&local_addr, 0, sizeof(local_addr));
//     local_addr.sin_family = AF_INET;
//     local_addr.sin_addr.s_addr = INADDR_ANY;
//     local_addr.sin_port = htons(14552);

//     if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
//         perror("bind failed");
//         close(sockfd);
//         return -1;
//     }

//     // 3. Set up remote address (GCS) at 14550
//     struct sockaddr_in remote_addr;
//     memset(&remote_addr, 0, sizeof(remote_addr));
//     remote_addr.sin_family = AF_INET;
//     remote_addr.sin_port = htons(14550);
//     if (inet_pton(AF_INET, remote_ip, &remote_addr.sin_addr) <= 0) {
//         perror("invalid remote IP");
//         close(sockfd);
//         return -1;
//     }

//     *out_sockfd = sockfd;
//     *out_remote = remote_addr;
//     return 0;  // success
// }

/**
 * @brief Send Mavlink Message to GCS
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> gcs_send_mavlink_message *
 */
void gcs_send_mavlink_message(unsigned int cpu_index, void *udata) {
    // if (!gcs_addr_initialized) {
    //     if (create_udp_client(&gcs_sockfd, &gcs_addr, gcs_ip) != 0) {
    //         fprintf(stderr, "Failed to create UDP client for GCS\n");
    //         return;
    //     }
    //     gcs_addr_initialized = 1;
    // }
    uint32_t message_id = (uint32_t)qemu_get_register(ARM_V7M_R1);
    uint32_t payload_ptr = (uint32_t)qemu_get_register(ARM_V7M_R2);
    uint8_t length;
    qemu_plugin_read_memory(qemu_get_register(ARM_V7M_SP), &length, 1);
    uint8_t crc_extra;
    qemu_plugin_read_memory(qemu_get_register(ARM_V7M_SP) + 4, &crc_extra, 1);
    uint8_t *payload = malloc(sizeof(uint8_t) * length);
    if (!payload) {
        fprintf(stderr, "Failed to allocate memory for mavlink payload\n");
        return;
    }
    qemu_plugin_read_memory(payload_ptr, payload, length);

    // send using mavlink_lib implementation of mav_finalize...
    int success = send_mavlink_payload(
        message_id,
        payload,
        length,
        crc_extra,
        &sequence_number
    );

    if (success != 0) {
        fprintf(stderr, "Failed to send mavlink message to GCS\n");
    }

    free(payload);

    // return from function and return 0
    qemu_set_register(0, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

int64_t last_switch_out_time_ns = 0;
int64_t last_switch_in_time_ns = 0;

/**
 * TODO: Put the address that this needs to be placed at
 * TODO: Verify the offsets used below
 */
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

    if (strcmp(thread2_name, "I2C1") == 0) {
        // switching out of I2C1 thread
        last_switch_out_time_ns = qemu_plugin_get_virtual_timer();
    } else if (strcmp(thread1_name, "I2C1") == 0) {
        // switching into I2C1 thread
        last_switch_in_time_ns = qemu_plugin_get_virtual_timer();
        int64_t elapsed_ns = last_switch_in_time_ns - last_switch_out_time_ns;
        printf("I2C1 thread was out for %ld us\n", elapsed_ns / 1000);
    }

    // printf("\nRegisters before switch:\n");

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

void print_r1(unsigned int cpu_index, void *udata) {
    uint32_t r1 = (uint32_t)qemu_get_register(ARM_V7M_R1);
    printf("(Delay) R1: 0x%08X\n", r1);
}

void ignore_cpu_failsafe_disarm(unsigned int cpu_index, void *udata) {
    // simply return from function
    uint32_t r1 = (uint32_t)qemu_get_register(ARM_V7M_R1);
    if (r1 == 6) { // 6 is the code for CPU failsafe disarm
        fprintf(stderr, "Ignoring CPU failsafe disarm call\n");
        qemu_set_register(1, ARM_V7M_R0); // return success
        uint32_t lr = qemu_get_register(ARM_V7M_LR);
        qemu_set_register(lr, ARM_V7M_PC);
    }
}


/*
AP File System Hooks for the AP_Logger_File Backend

int open(const char *fname, int flags, bool allow_absolute_paths = false);
int close(int fd);
int32_t read(int fd, void *buf, uint32_t count);
int32_t write(int fd, const void *buf, uint32_t count);
int fsync(int fd);

Should be opening these all under a single directory like flight_logs/

All virtuals should be called like this:

<address/symbol> ap_fs_open *
<address/symbol> ap_fs_close *
<address/symbol> ap_fs_read *
<address/symbol> ap_fs_write *
<address/symbol> ap_fs_fsync *
*/

void ap_fs_open(unsigned int cpu_index, void *udata) {
    uint32_t fname_ptr = (uint32_t)qemu_get_register(ARM_V7M_R1);
    // uint32_t flags = (uint32_t)qemu_get_register(ARM_V7M_R2);
    // bool allow_absolute_paths = false;

    char fname[256];
    memset(fname, 0, sizeof(fname));
    qemu_plugin_read_memory(fname_ptr, (uint8_t*)fname, sizeof(fname));

    // printf("Opening file: /root/rooney/FastDyn/courbet/flight_logs/%s\n", fname);

    char path[256];
    snprintf(path, sizeof(path) + 41, "/root/rooney/FastDyn/courbet/flight_logs/%s", fname);

    int fd = open(path, O_RDWR | O_CREAT, 0666);
    mark_open_flight_log_fd(fd);
    qemu_set_register(fd, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

void ap_fs_close(unsigned int cpu_index, void *udata) {
    uint32_t fd = (uint32_t)qemu_get_register(ARM_V7M_R1);
    int result = close(fd);
    mark_close_flight_log_fd(fd);
    qemu_set_register(result, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

void ap_fs_read(unsigned int cpu_index, void *udata) {
    uint32_t fd = (uint32_t)qemu_get_register(ARM_V7M_R1);
    uint32_t buf_ptr = (uint32_t)qemu_get_register(ARM_V7M_R2);
    uint32_t count = (uint32_t)qemu_get_register(ARM_V7M_R3);
    char *buf = (char *)malloc(count);
    if (!buf) {
        fprintf(stderr, "Failed to allocate memory for read\n");
        qemu_set_register(-1, ARM_V7M_R0);
        uint32_t lr = qemu_get_register(ARM_V7M_LR);
        qemu_set_register(lr, ARM_V7M_PC);
        return;
    }
    ssize_t result = read(fd, buf, count);
    qemu_plugin_write_memory(buf_ptr, (uint8_t*)buf, count);
    free(buf);
    qemu_set_register(result, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

void ap_fs_write(unsigned int cpu_index, void *udata) {
    uint32_t fd = (uint32_t)qemu_get_register(ARM_V7M_R1);
    uint32_t buf_ptr = (uint32_t)qemu_get_register(ARM_V7M_R2);
    uint32_t count = (uint32_t)qemu_get_register(ARM_V7M_R3);
    char *buf = (char *)malloc(count);
    if (!buf) {
        fprintf(stderr, "Failed to allocate memory for write\n");
        qemu_set_register(-1, ARM_V7M_R0);
        uint32_t lr = qemu_get_register(ARM_V7M_LR);
        qemu_set_register(lr, ARM_V7M_PC);
        return;
    }
    qemu_plugin_read_memory(buf_ptr, (uint8_t*)buf, count);
    ssize_t result = write(fd, buf, count);
    free(buf);
    qemu_set_register(result, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}

void ap_fs_fsync(unsigned int cpu_index, void *udata) {
    uint32_t fd = (uint32_t)qemu_get_register(ARM_V7M_R1);
    int result = fsync(fd);
    qemu_set_register(result, ARM_V7M_R0);
    uint32_t lr = qemu_get_register(ARM_V7M_LR);
    qemu_set_register(lr, ARM_V7M_PC);
}
