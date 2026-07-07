#pragma once

#include <stdint.h>

typedef struct {
    int16_t ax_lsb;
    int16_t ay_lsb;
    int16_t az_lsb;
    int16_t gx_lsb;
    int16_t gy_lsb;
    int16_t gz_lsb;
    int16_t temp_lsb;
} BoardrunnerImuSample;

/*
 * Returns a raw sensor-frame sample in chip LSB units. Device models are
 * responsible for packing this into their register and FIFO layouts.
 *
 * Runtime overrides:
 *   BOARDRUNNER_IMU_AX, BOARDRUNNER_IMU_AY, ...
 *   BOARDRUNNER_IMU_<DEVICE>_AX, BOARDRUNNER_IMU_<DEVICE>_AY, ...
 *
 * Example:
 *   BOARDRUNNER_IMU_ICM20602_AZ=-2048
 */
void boardrunner_imu_get_sample(const char *device_name, BoardrunnerImuSample *sample);

