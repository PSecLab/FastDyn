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

// TODO: Add existing virtual functions here from ArduRover on Vanilla HALucinator

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
    qemu_plugin_write_memory(this_pointer + 0x18, (const uint8_t *)&wheel_radius, sizeof(int));
    qemu_plugin_write_memory(this_pointer + 0x1C, (const uint8_t *)&wheel_radius, sizeof(float));

    // Set proper wheel type for both wheels
    uint32_t this_pointer = (uint32_t)qemu_get_register(ARM_V7M_R0);
    uint8_t type = 1;
    qemu_plugin_write_memory(this_pointer, (const uint8_t *)&type, sizeof(uint8_t));
    qemu_plugin_write_memory(this_pointer + 1, (const uint8_t *)&type, sizeof(uint8_t));
}

void copy_wheel_encoder_state_to_frontend(unsigned int cpu_index, void *udata)
{
    // TODO: Implement copying wheel encoder state to frontend
}

