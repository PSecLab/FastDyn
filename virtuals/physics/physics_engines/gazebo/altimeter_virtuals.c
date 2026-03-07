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
#include <stdio.h>
#include "virtuals.h"
#include "gazebo.h"
#include "phy.h"
#include <math.h>
// #include "mavlink_lib.h"
// #include <arpa/inet.h>
// #include <stdatomic.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

static void catch_up(void *opaque)
{
    (void) opaque;

    double target_time_s = (double)qemu_plugin_get_virtual_timer() / 1e9;

    if (!phy_advance_simulation(target_time_s)) {
        fprintf(stderr, "advance_simulation failed in catch_up\n");
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
void start_advancing_sim_periodic(unsigned int cpu_index, void *udata) {
    // Start a periodic timer to advance the simulation
    qemu_plugin_timer_new_period_ns(catch_up, NULL, 1e8);
}

/**
 * @brief Get an altimeter reading in centimeters from the barometer
 *
 * Called like this from virtuals.txt:
 *
 * <address/symbol> get_barometer_altitude_cm *
 */
void get_barometer_altitude_cm(unsigned int cpu_index, void *udata)
{
    double altitude_cm = 0.0;
    if (!phy_get_altimeter_reading(&altitude_cm))
    {
        fprintf(stderr, "Failed to get barometer altitude reading\n");
        altitude_cm = -1.0; // indicate error
    }
    // write altitude back to r0
    // qemu_set_register(*(uint32_t *)&altitude_cm, ARM_V7M_R0);
    // uint32_t lr = qemu_get_register(ARM_V7M_LR);
    // qemu_set_register(lr, ARM_V7M_PC);
}

int virtual_gz_altimeter_init(int argc, char **argv) {
    // Nothing to initialize for now
    virtual_register("get_barometer_altitude_cm", get_barometer_altitude_cm);
    virtual_register("start_advancing_sim_periodic", start_advancing_sim_periodic);

    return 0;
}