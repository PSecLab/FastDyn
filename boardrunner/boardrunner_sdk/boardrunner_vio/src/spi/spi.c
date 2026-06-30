#include "spi_internal.h"
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>

static bool api_spi_debug_enabled(void) {
    static int initialized = 0;
    static bool enabled = false;

    if (!initialized) {
        const char *env = getenv("BOARD_RUNNER_SPI_DEBUG");
        enabled = (env != NULL && env[0] != '\0' && env[0] != '0');
        initialized = 1;
    }

    return enabled;
}

static SpiSlaveDetails *api_spi_find_slave_by_cs(SPIBus *bus, int cs_id) {
    if (!bus) {
        return NULL;
    }

    for (int i = 0; i < bus->Slaves.num_slaves; i++) {
        SpiSlaveDetails *slave = &bus->Slaves.slave[i];
        if (slave->cs_id == cs_id) {
            return slave;
        }
    }

    return NULL;
}

static SpiSlaveDetails *api_spi_first_active_slave(SPIBus *bus) {
    if (!bus) {
        return NULL;
    }

    for (int i = 0; i < bus->Slaves.num_slaves; i++) {
        SpiSlaveDetails *slave = &bus->Slaves.slave[i];
        if (slave->cs_enable == 1) {
            return slave;
        }
    }

    return NULL;
}

/**
 * @brief Initializes an SPI bus by parsing slave device configuration.
 *
 * @param model_info The configuration section containing device information.
 * @return An initialized SPIBus struct. If initialization fails, the
 * returned struct will be empty (num_slaves will be 0).
 */
SPIBus api_spi_init_bus(ConfigSection* model_info, const char* bus_name) {
    SPIBus bus = {0};
    bool success;
    success = spi_slave_device_parse(&bus, model_info, bus_name); //parses all the attached slave devices
    if (!success) {
        utils_die("Unable to parse the slave devices attached to I2C");
    }
    return bus;
}

/**
 * @brief Sets the logic state of a specific chip select (CS) line.
 *
 * This function iterates through all registered slaves. For any slave that
 * matches the target 'cs_id', it updates the bus's internal 'cs_enable'
 * state for that slave and then calls the slave's 'set_cs' callback function
 * to notify it of the change.
 *
 * @param bus A pointer to the SPIBus structure.
 * @param cs_id The identifier of the chip select line to modify.
 * @param level The new logic level of the line (0 = active, 1 = inactive).
 */
void api_spi_set_cs(SPIBus *bus, int cs_id, int level) {
    if (!bus) {
        return;
    }

    int new_enable_state = (level == 0) ? 1 : 0;
    bool found = false;

    for (int i = 0; i < bus->Slaves.num_slaves; i++) {
        SpiSlaveDetails* current_slave = &bus->Slaves.slave[i];

        if (current_slave->cs_id == cs_id) {
            found = true;

            if (current_slave->cs_enable == new_enable_state) {
                continue;
            }
            current_slave->cs_enable = new_enable_state;

            if (current_slave->set_cs) {
                current_slave->set_cs(level);
            }
        }
    }

    if (!found && api_spi_debug_enabled()) {
        fprintf(stderr, "SPI warning: CS id %d is not attached to this bus\n", cs_id);
    }
}

/**
 * @brief Select one SPI slave and deselect all other slaves on the bus.
 *
 * This is preferred over direct api_spi_set_cs(..., 0) in generated bus models
 * when the firmware asserts a specific chip select. It is edge-aware: selecting
 * an already-selected slave does not re-call the slave set_cs callback, so slave
 * command state is preserved across split DMA/PIO chunks while CS remains low.
 */
bool api_spi_select(SPIBus *bus, int cs_id) {
    bool found = false;

    if (!bus) {
        return false;
    }

    for (int i = 0; i < bus->Slaves.num_slaves; i++) {
        SpiSlaveDetails *slave = &bus->Slaves.slave[i];

        if (slave->cs_id == cs_id) {
            found = true;
            continue;
        }

        if (slave->cs_enable == 1) {
            api_spi_set_cs(bus, slave->cs_id, 1);
        }
    }

    if (!found) {
        if (api_spi_debug_enabled()) {
            fprintf(stderr, "SPI warning: cannot select missing CS id %d\n", cs_id);
        }
        return false;
    }

    api_spi_set_cs(bus, cs_id, 0);
    return true;
}

/**
 * @brief Deselect one SPI slave by driving its CS line inactive/high.
 */
bool api_spi_deselect(SPIBus *bus, int cs_id) {
    if (!api_spi_find_slave_by_cs(bus, cs_id)) {
        if (api_spi_debug_enabled()) {
            fprintf(stderr, "SPI warning: cannot deselect missing CS id %d\n", cs_id);
        }
        return false;
    }

    api_spi_set_cs(bus, cs_id, 1);
    return true;
}

/**
 * @brief Return the active CS id, -1 if none, or -2 if multiple slaves are active.
 */
int api_spi_active_cs(const SPIBus *bus) {
    int active_cs = -1;
    int active_count = 0;

    if (!bus) {
        return -1;
    }

    for (int i = 0; i < bus->Slaves.num_slaves; i++) {
        const SpiSlaveDetails *slave = &bus->Slaves.slave[i];
        if (slave->cs_enable == 1) {
            active_cs = slave->cs_id;
            active_count++;
        }
    }

    if (active_count > 1) {
        return -2;
    }

    return active_cs;
}

/**
 * @brief Return how many slaves are currently selected on a bus.
 */
int api_spi_active_count(const SPIBus *bus) {
    int active_count = 0;

    if (!bus) {
        return 0;
    }

    for (int i = 0; i < bus->Slaves.num_slaves; i++) {
        if (bus->Slaves.slave[i].cs_enable == 1) {
            active_count++;
        }
    }

    return active_count;
}

/**
 * @brief Performs a full-duplex SPI transfer with the active slave.
 *
 * This function scans the bus for a slave that is currently active
 * (i.e., 'cs_enable' is 1). It calls the 'transfer' function of the
 * first active slave it finds, passing the master's data 'val' (MOSI).
 * It then returns the 32-bit value received from that slave (MISO).
 *
 * @param bus A pointer to the SPIBus structure.
 * @param val The 32-bit data word sent from the master (MOSI).
 * @return uint32_t The 32-bit data word received from the slave (MISO).
 * Returns 0xFFFFFFFF (idle line) if no slave is active.
 */
uint32_t api_spi_transfer(SPIBus *bus, uint32_t val) {
    uint32_t ret_val = 0xFFFFFFFF;
    if (!bus) {
        return ret_val;
    }

    int active_count = api_spi_active_count(bus);
    if (active_count == 0) {
        if (api_spi_debug_enabled()) {
            fprintf(stderr, "SPI warning: transfer with no active slave, MOSI=0x%02x\n",
                    (unsigned)(val & 0xFFU));
        }
        return ret_val;
    }
    if (active_count > 1 && api_spi_debug_enabled()) {
        fprintf(stderr, "SPI warning: transfer with %d active slaves; using first active slave\n",
                active_count);
    }

    SpiSlaveDetails *current_slave = api_spi_first_active_slave(bus);
    if (!current_slave) {
        return ret_val;
    }

    if (!current_slave->transfer) {
        fprintf(stderr, "SPI Error: Slave '%s' is active but has no transfer function.\n",
                current_slave->name ? current_slave->name : "unknown");
        return ret_val;
    }

    return current_slave->transfer(val);
}

/**
 * @brief Byte-oriented wrapper for api_spi_transfer.
 */
uint8_t api_spi_transfer_byte(SPIBus *bus, uint8_t val) {
    return (uint8_t)(api_spi_transfer(bus, val) & 0xFFU);
}

/**
 * @brief Transfer a contiguous SPI burst without changing CS state.
 *
 * The caller owns CS assertion/deassertion. This helper exists so generated DMA
 * handlers can preserve transaction state across multi-byte bursts instead of
 * manually reselecting slaves for each byte. If tx is NULL, 0xFF is clocked.
 * If rx is NULL, received bytes are discarded.
 *
 * Returns the number of transferred bytes, or -1 on invalid arguments or when
 * no slave is active.
 */
int api_spi_transfer_buf(SPIBus *bus, const uint8_t *tx, uint8_t *rx, size_t len) {
    if (!bus || len == 0U) {
        return -1;
    }
    if (api_spi_active_count(bus) == 0) {
        if (api_spi_debug_enabled()) {
            fprintf(stderr, "SPI warning: buffer transfer with no active slave, len=%zu\n", len);
        }
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t tx_byte = tx ? tx[i] : 0xFFU;
        uint8_t rx_byte = api_spi_transfer_byte(bus, tx_byte);
        if (rx) {
            rx[i] = rx_byte;
        }
    }

    return (int)len;
}
