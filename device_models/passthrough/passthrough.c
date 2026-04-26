/*
 * Passthrough device model.
 *
 * Forwards every MMIO read/write and serves every IRQ via a shared
 * hw_session (see device_models/common/hw_session.h). All probe
 * ownership, the IRQ-poll thread, and the halted-write retry loop now
 * live in hw_session, so this file is a thin adapter between the
 * DeviceModel ABI and the session.
 */

#include <device.h>
#include <utils.h>

#include "../common/hw_session.h"

static hw_session_t *s_session;

static uint64_t passthrough_read(void *opaque, hwaddr address,
                                 unsigned size, uint64_t pc)
{
    (void)opaque;
    uint64_t value = 0;
    if (hw_session_read(s_session, address, size, &value, pc) != 0) {
        utils_die("HW Read Failed");
    }
    return value;
}

static void passthrough_write(void *opaque, hwaddr address,
                              uint64_t value, unsigned size, uint64_t pc)
{
    (void)opaque;
    if (hw_session_write(s_session, address, value, size, pc) != 0) {
        utils_die("HW Write Failed");
    }
}

static int passthrough_serve(int line)
{
    return hw_session_serve(s_session, line);
}

static int passthrough_interrupt(int line)
{
    (void)line;
    return 0;
}

static void *passthrough_init(ConfigSection *model_info);

DeviceModel passthrough_model_def = {
    .name      = "passthrough",
    .read      = passthrough_read,
    .write     = passthrough_write,
    .init      = passthrough_init,
    .serve     = passthrough_serve,
    .interrupt = passthrough_interrupt,
};

static void *passthrough_init(ConfigSection *model_info)
{
    Range ranges[10];
    utils_parse_ranges(model_info->overall_range_count,
                       model_info->overall_ranges, ranges);

    s_session = hw_session_acquire(model_info->backend);
    if (!s_session) {
        utils_die("HW connection failed.");
        return NULL;
    }

    for (int i = 0; i < model_info->overall_range_count; i++) {
        dev_register_device_model(ranges[i].start, ranges[i].end,
                                  &passthrough_model_def);
    }

    /*
     * Register the device model for the IRQs that user-declared devices
     * own. NOTE: the trailing break preserves the pre-refactor behavior
     * (only the first device's IRQs are registered). The TODO inherited
     * from before this refactor still applies; out of scope here.
     */
    for (int di = 0; di < model_info->device_count; di++) {
        DeviceModels *d = &model_info->devices[di];

        if (d->irq_count > 0 && d->irqs) {
            for (int j = 0; j < d->irq_count; j++) {
                dev_register_interrupt_device_model((int)d->irqs[j],
                                                    &passthrough_model_def);
            }
        }

        /* TODO: register for all devices, not just the first. */
        break;
    }

    return NULL;
}
