#include <device.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <inttypes.h> // For uint8_t
#include "devmodels_apis.h"
#include "utils.h"
// Helper functions

/**
 * @brief This function parses the device models to configure SPI slave devices.
 * It is called once when initializing the model to populate the SPI bus with its slaves.
 *
 * @param bus The SPI bus structure to populate.
 * @param model_info The configuration section containing device information.
 * @return true on successful parsing, false on failure.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

// Assume all necessary struct definitions (SPIBus, ConfigSection, etc.) are included here

bool spi_slave_device_parse(SPIBus* bus, ConfigSection* model_info) {
    if (!bus || !model_info) {
        fprintf(stderr, "Error: Invalid arguments passed to spi_slave_device_parse.\n");
        return false;
    }

    for (int i = 0; i < model_info->device_count; i++) {
        DeviceModels* current_dev = &model_info->devices[i];

        if (strstr(current_dev->name, "spi") != NULL) {
            // Use the specific type: SpiSlaveList
            SpiSlaveList* slavelist = calloc(1, sizeof(SpiSlaveList));
            if (!slavelist) {
                perror("Failed to allocate memory for SpiSlaveList");
                return false;
            }

            SPIDevices spi_details = current_dev->SPIdetails;

            for (int j = 0; j < spi_details.slave_count; j++) {
                SPISlaveDevice* curr_slave_config = &spi_details.spi_slave[j];

                if (curr_slave_config->is_scroll_path) {
                    void* scroll_handle = dlopen(curr_slave_config->scroll_path, RTLD_NOW);
                    if (!scroll_handle) {
                        fprintf(stderr, "Unable to open slave device scroll '%s': %s\n",
                                curr_slave_config->scroll_path, dlerror());
                        free(slavelist->slave);
                        free(slavelist);
                        return false;
                    }

                    slavelist->num_slaves++;
                    // Use the specific type in realloc and sizeof: SpiSlaveDetails
                    SpiSlaveDetails* new_slave_array = realloc(slavelist->slave, slavelist->num_slaves * sizeof(SpiSlaveDetails));
                    if (!new_slave_array) {
                        perror("Failed to reallocate memory for slave array");
                        dlclose(scroll_handle);
                        free(slavelist->slave);
                        free(slavelist);
                        return false;
                    }
                    slavelist->slave = new_slave_array;

                    // This line is now correct because all types match
                    SpiSlaveDetails* new_slave = &slavelist->slave[slavelist->num_slaves - 1];
                    // Use the specific type in sizeof: SpiSlaveDetails
                    memset(new_slave, 0, sizeof(SpiSlaveDetails));

                    new_slave->name = curr_slave_config->slave_name;
                    new_slave->cs_id = curr_slave_config->cs_id;
                    new_slave->cs_enable = 0;

                    char symbol[256];

                    snprintf(symbol, sizeof(symbol), "%s_transfer", curr_slave_config->slave_name);
                    new_slave->transfer = (SlaveTransferFunc)dlsym(scroll_handle, symbol);
                    if (!new_slave->transfer) {
                        fprintf(stderr, "Missing transfer function ('%s') in slave model '%s'\n", symbol, curr_slave_config->scroll_path);
                        dlclose(scroll_handle);
                        free(slavelist->slave);
                        free(slavelist);
                        return false;
                    }

                    snprintf(symbol, sizeof(symbol), "%s_set_cs", curr_slave_config->slave_name);
                    new_slave->set_cs = (SlaveSetcsFunc)dlsym(scroll_handle, symbol);
                    if (!new_slave->set_cs) {
                        fprintf(stderr, "Missing set_cs function ('%s') in slave model '%s'\n", symbol, curr_slave_config->scroll_path);
                        dlclose(scroll_handle);
                        free(slavelist->slave);
                        free(slavelist);
                        return false;
                    }
                }
            }

            // This assignment is now correct because both sides are of type SpiSlaveList
            bus->Slaves = *slavelist;
            free(slavelist);

            return true;
        }
    }

    fprintf(stderr, "Error: Configuration parsing failed. No 'spi' device found.\n");
    return false;
}