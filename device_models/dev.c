#include <qemu/qemu-plugin.h>
#include <time.h>
#include <utils.h>
#include <device.h>
#include <core.h>
#include "models.c"
#include "cJSON.h"

// Global current device model
static FILE * io_logger;

typedef struct DeviceNode {
    DeviceModel *dev;
    struct DeviceNode *next;
} DeviceNode;
static const char* read_priority_device_name = "elder";

static struct timespec start_ts;
static DeviceNode *device_lut[NUM_SLOTS] = {0};
static DeviceNode *system_lut[SYSTEM_NUM_SLOTS] = {0};
static DeviceNode *ram_lut[RAM_NUM_SLOTS] = {0};

static inline DeviceNode **dev_select_lut(hwaddr addr) {
    switch (addr >> 28) { // top 4 bits
        case 0x4:
		case 0x5:
			return device_lut;  // Peripheral MMIO
        case 0xE:
			return system_lut;  // System Control Block
		case 0x2:
			return ram_lut; //RAM
        default:
			return NULL;        // unknown region
    }
}

static inline unsigned dev_addr_to_slot(hwaddr addr, hwaddr REGION_BASE)
{
    return (addr - REGION_BASE) / SLOT_SIZE;
}

//Interrupt LUT
static DeviceModel *irq_lut[MAX_INTERRUPTS] = {0}; //Initialize all to NULL

static inline void dev_get_timestamp(time_t *sec, long *usec) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	// Calculate offset from start
	*sec = ts.tv_sec - start_ts.tv_sec;
	*usec = (ts.tv_nsec - start_ts.tv_nsec) / 1000;
	if (*usec < 0) {
    	*sec -= 1;
	    *usec += 1000000;
	}
}

static int dev_write(char * handler, long unsigned int address, uint64_t value, long unsigned int size) {
#ifdef DEV_LOGGER
    uint64_t pc = core_get_pc();
    time_t sec;
    long usec;
    dev_get_timestamp(&sec, &usec);
#endif
    DeviceNode **lut = dev_select_lut(address);
    if (!lut) {

#ifdef DEV_LOGGER
        utils_log_to_file(io_logger,"[%5ld.%06ld] IO Write Access NOT Handled (Unknown Region): \t address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 ", pc=0x%08X \n",
                        sec, usec, address, size, size * 2, value, pc);
#endif
        return 1; // Continue internal operation
    }

    hwaddr region_base = (lut == device_lut) ? DEVICE_BASE : SYSTEM_BASE;
    unsigned max_slots = (lut == device_lut) ? NUM_SLOTS : SYSTEM_NUM_SLOTS;
    unsigned idx = dev_addr_to_slot(address, region_base);

    DeviceNode *node = (idx < max_slots) ? lut[idx] : NULL;
    bool handled = false;

    // Iterate through all devices registered at this address
    while (node) {
        DeviceModel *dev = node->dev;
        if (dev) {
            dev->write(dev->opaque, address, value, size, pc);
            handled = true;
#ifdef DEV_LOGGER
            utils_log_to_file(io_logger,"[%5ld.%06ld] [%s] Write: \t address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 ", pc=0x%08X \n",
                    sec, usec, dev->name, address, size, size * 2, value, pc);
#endif
        }
        node = node->next;
    }

    if (!handled) {
#ifdef DEV_LOGGER
        utils_log_to_file(io_logger,"[%5ld.%06ld] IO Write Access NOT Handled: \t address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 ", pc=0x%08X \n",
                        sec, usec, address, size, size * 2, value, pc);
#endif
        dev_debug("IO Access not handled");
    }

	//This is fix so we don't inject two interrupts
    if (handler && (handler[0] == 'g' || handler[0] == 'v')) {
           return 0;
    }

    // Continue internal operation
    return 1;
}

// TODO: Change the hardcoded priorty variable.
static int dev_read(char * handler, long unsigned int address, uint64_t *buf, long unsigned int size) {
#ifdef DEV_LOGGER
    uint64_t pc = core_get_pc();
    time_t sec;
    long usec;
    dev_get_timestamp(&sec, &usec);
#endif
    uint64_t value = 0; // Default value
    DeviceNode **lut = dev_select_lut(address);

    if (!lut) {
#ifdef DEV_LOGGER
        utils_log_to_file(io_logger,
            "[%5ld.%06ld] IO Read Access NOT Handled (Unknown Region): \t address=0x%08" PRIx64 ", size=%u bytes, pc=0x%08" PRIx64 "\n",
            sec, usec, (uint64_t)address, size, (uint64_t)pc);
#endif
        return 1;
    }

    hwaddr region_base = (lut == device_lut) ? DEVICE_BASE : SYSTEM_BASE;
    unsigned max_slots = (lut == device_lut) ? NUM_SLOTS : SYSTEM_NUM_SLOTS;
    unsigned idx = dev_addr_to_slot(address, region_base);

    DeviceNode *node = (idx < max_slots) ? lut[idx] : NULL;
    DeviceModel *dev_to_use = NULL;

    if (node) { // Check if at least one device exists
        if (node->next == NULL) {
            // Case 1: Only one device registered. Use it without checking priority.
            dev_to_use = node->dev;
        } else {
            // Case 2: Multiple devices registered. Find priority, or default to first.
            DeviceNode *current = node;
            dev_to_use = node->dev; // Default to the first device found

            while (current) {
                if (current->dev && current->dev->name && strcmp(current->dev->name, read_priority_device_name) == 0) {
                    dev_to_use = current->dev; // Found the priority device, override default
                    break;
                }
                current = current->next;
            }
        }
    }

    if (dev_to_use) {
        // A device was selected (either the single one, or the priority one from a list)
        value = dev_to_use->read(dev_to_use->opaque, address, size, pc);
        *buf = value;
#ifdef DEV_LOGGER
        utils_log_to_file(io_logger, "[%5ld.%06ld] [%s] Read: \t address = 0x%08X, size = %u bytes, value = 0x%0*" PRIx64 ", pc=0x%08X \n",
              sec, usec, dev_to_use->name, address, size, size * 2, value, pc);
#endif
    } else {
        // This block is reached if:
        // 1. No devices were registered at all (node was NULL).
        // 2. Multiple devices were registered, but none matched the priority name.
#ifdef DEV_LOGGER
        utils_log_to_file(io_logger,
            "[%5ld.%06ld] IO Read Access NOT Handled: \t address=0x%08" PRIx64 ", size=%u bytes, pc=0x%08" PRIx64 "\n",
            sec, usec, (uint64_t)address, size, (uint64_t)pc);
#endif
        dev_debug("IO Access not handled");
    }

    if (handler && (handler[0] == 'g')) {
        return 0;
    }

    // Continue internal operation
    return 1;
}

// Base hardware reader and writer
static DEV_XPORTER importer = {
        .read = dev_read,
        .write = dev_write
};

DeviceModel* find_device_model(const char *name) {
    for (int i = 0; i < num_devices; i++) {
        if (strcmp(all_devices[i]->name, name) == 0) {
            return all_devices[i];
        }
    }
    return NULL;
}

void dev_notify_irq(int number) {
	time_t sec;
    long usec;
    dev_get_timestamp(&sec, &usec);
#ifdef DEV_LOGGER
	utils_log_to_file(io_logger, "[%5ld.%06ld] Interrupt Taken: \t Vector = 0x%08X\n",
              sec, usec, number);
#endif
	DeviceModel* dev = irq_lut[number];
	if (dev != NULL) {
		dev->interrupt(number);
	}
	else {
		utils_log_to_file(io_logger, "Skipping IRQ NUM %d handling as not registered by the user\n", number);
		printf("Skipping IRQ NUM %d handling as not registered by the user\n", number);
	}
}

void dev_irqret_hook(int number) {
    time_t sec;
    long usec;
    dev_get_timestamp(&sec, &usec);
    utils_log_to_file(io_logger, "[%5ld.%06ld] Interrupt Served: \t Vector = 0x%08X\n",
              sec, usec, number);

	DeviceModel* dev = irq_lut[number];
	if (dev != NULL) {
		dev->serve(number);
	}
	else {
		utils_log_to_file(io_logger, "Skipping IRQ NUM %d handling as not registered by the user\n", number);
		printf("Skipping IRQ NUM %d handling as not registered by the user\n", number);
	}
}

void dev_register_device_model(hwaddr start, hwaddr end, DeviceModel *dev) {
    DeviceNode **lut = dev_select_lut(start);
    if (!lut) return; // Address range not supported

    hwaddr region_base = (lut == device_lut) ? DEVICE_BASE : SYSTEM_BASE;
    unsigned idx_start = dev_addr_to_slot(start, region_base);
    unsigned idx_end   = dev_addr_to_slot(end, region_base);

    unsigned max_slots = (lut == device_lut) ? NUM_SLOTS : SYSTEM_NUM_SLOTS;
    if (idx_end >= max_slots) idx_end = max_slots - 1;

    for (unsigned i = idx_start; i <= idx_end; i++) {
        // Create a new node for the linked list
        DeviceNode *newNode = (DeviceNode *)malloc(sizeof(DeviceNode));
        if (!newNode) {
            utils_die("Failed to allocate memory for device node");
        }
        newNode->dev = dev;

        // Prepend the new node to the list at this slot
        newNode->next = lut[i];
        lut[i] = newNode;
    }
}

void dev_register_interrupt_device_model(int irq_num, DeviceModel *dev) {
    // For the passed interrupt number, register the device model
    if (irq_num < 0 || irq_num >= MAX_INTERRUPTS) {
        printf("ERROR! IRQ NUM: %d not supported for the given device!\n", irq_num);
		utils_die("Reconfigure the device model\n");
    }

    irq_lut[irq_num] = dev;  // register the device
}

char* dev_read_json_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* data = (char*)malloc(length + 1);
    fread(data, 1, length, f);
    data[length] = '\0';
    fclose(f);

    return data;
}

// Helper function to parse a cJSON string array into a char**
void dev_parse_string_array(cJSON* json_array, int* count, char*** target) {
    *count = cJSON_GetArraySize(json_array);
    if (*count == 0) return;

    *target = malloc(*count * sizeof(char*));
    if (!*target) { /* handle error */ *count = 0; return; }

    int i = 0;
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, json_array) {
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            (*target)[i++] = strdup(item->valuestring);
        }
    }
}

void dev_free_config(AppConfig* config) {
    if (!config) return;

    for (int i = 0; i < config->section_count; i++) {
        ConfigSection* section = &config->sections[i];
        free(section->name);
        free(section->backend);

        for (int j = 0; j < section->overall_range_count; j++) {
            free(section->overall_ranges[j]);
        }
        free(section->overall_ranges);

        for (int j = 0; j < section->device_count; j++) {
            DeviceModels* device = &section->devices[j];
            free(device->name);
            free(device->scroll_path);
            free(device->irq);
            for (int k = 0; k < device->range_count; k++) {
                free(device->ranges[k]);
            }
            free(device->ranges);
        }
        free(section->devices);
    }
    free(config->sections);
    free(config);
}

/**
 * @brief This function parses the main JSON configuration string to build a structured
 * representation of all device models and their settings.
 *
 * @note This function assumes that the `DeviceModels` struct has been updated
 * to include an `SPIdetails` member, similar to the existing `I2Cdevices` one,
 * to hold SPI slave configurations.
 *
 * @param json_string The raw JSON configuration as a string.
 * @return A pointer to a fully populated AppConfig struct, or NULL on failure.
 */
AppConfig* dev_parse_json_configs(const char* json_string) {
    cJSON* root = cJSON_Parse(json_string);
    if (!root) {
        fprintf(stderr, "JSON parse error: %s\n", cJSON_GetErrorPtr());
        return NULL;
    }

    // Allocate the main config struct
    AppConfig* app_config = calloc(1, sizeof(AppConfig));
    if (!app_config) {
        fprintf(stderr, "Failed to allocate memory for AppConfig\n");
        cJSON_Delete(root);
        return NULL;
    }

    // Iterate through top-level sections ("elder", "passthrough", etc.)
    cJSON* section_item = NULL;
    cJSON_ArrayForEach(section_item, root) {
        // Grow the sections array
        app_config->section_count++;
        ConfigSection* new_sections = realloc(app_config->sections, app_config->section_count * sizeof(ConfigSection));
        if (!new_sections) { /* handle realloc failure */ cJSON_Delete(root); free(app_config); return NULL; }
        app_config->sections = new_sections;

        ConfigSection* current_section = &app_config->sections[app_config->section_count - 1];
        memset(current_section, 0, sizeof(ConfigSection));
        current_section->name = strdup(section_item->string);

        // Iterate through items within the section (overall, backend, devices)
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, section_item) {
            const char* key = item->string;
            if (strcmp(key, "overall") == 0 && cJSON_IsArray(item)) {
                dev_parse_string_array(item, &current_section->overall_range_count, &current_section->overall_ranges);
            } else if (strcmp(key, "backend") == 0) {
                if (cJSON_IsString(item)) {
                    current_section->backend = strdup(item->valuestring);
                }
            } else { // Assume it's a device model (e.g., "i2c", "spi", "unhandled_space")
                // Grow the devices array for this section
                current_section->device_count++;
                DeviceModels* new_devices = realloc(current_section->devices, current_section->device_count * sizeof(DeviceModels));
                if (!new_devices) { /* handle realloc failure */ cJSON_Delete(root); free(app_config); return NULL; }
                current_section->devices = new_devices;

                DeviceModels* current_device = &current_section->devices[current_section->device_count - 1];
                memset(current_device, 0, sizeof(DeviceModels));
                current_device->name = strdup(key);

                // Parse common device properties
                cJSON* scroll = cJSON_GetObjectItemCaseSensitive(item, "scroll");
                if (cJSON_IsString(scroll)) {
                    current_device->scroll_path = strdup(scroll->valuestring);
                }
                cJSON* range = cJSON_GetObjectItemCaseSensitive(item, "range");
                if (cJSON_IsArray(range)) {
                    dev_parse_string_array(range, &current_device->range_count, &current_device->ranges);
                }
                cJSON* irq = cJSON_GetObjectItemCaseSensitive(item, "irq");
                if (cJSON_IsString(irq)) {
                    current_device->irq = strdup(irq->valuestring);
                }

                // --- Peripheral-specific slave parsing ---
                cJSON* slaves_obj = cJSON_GetObjectItemCaseSensitive(item, "slaves");
                if (cJSON_IsObject(slaves_obj)) {
                    cJSON* slave_item = NULL;

                    // >>> I2C SLAVE PARSING <<<
                    if (strstr(current_device->name, "i2c") != 0) {
                        cJSON_ArrayForEach(slave_item, slaves_obj) {
                            current_device->I2Cdevices.device_count++;
                            current_device->I2Cdevices.i2cdevice = realloc(current_device->I2Cdevices.i2cdevice, current_device->I2Cdevices.device_count * sizeof(I2CDevice));
                            I2CDevice* current_slave = &current_device->I2Cdevices.i2cdevice[current_device->I2Cdevices.device_count - 1];
                            memset(current_slave, 0, sizeof(I2CDevice));

                            current_slave->slave_name = strdup(slave_item->string);
                            cJSON* addr = cJSON_GetObjectItemCaseSensitive(slave_item, "address");
                            if (cJSON_IsString(addr)) {
                                current_slave->address = (int)strtol(addr->valuestring, NULL, 0);
                            }
                            cJSON* slave_scroll = cJSON_GetObjectItemCaseSensitive(slave_item, "scroll_path");
                            if (cJSON_IsString(slave_scroll)) {
                                current_slave->scroll_path = strdup(slave_scroll->valuestring);
                            }
                            cJSON* is_scroll = cJSON_GetObjectItemCaseSensitive(slave_item, "is_scroll_path");
                            if (cJSON_IsBool(is_scroll)) {
                               current_slave->_is_scroll = cJSON_IsTrue(is_scroll);
                            }
                        }
                    }

                    // >>> SPI SLAVE PARSING <<<
                    else if (strstr(current_device->name, "spi") != 0) {
                        cJSON_ArrayForEach(slave_item, slaves_obj) {
                            current_device->SPIdetails.slave_count++;
                            current_device->SPIdetails.spi_slave = realloc(current_device->SPIdetails.spi_slave, current_device->SPIdetails.slave_count * sizeof(SPISlaveDevice));
                            SPISlaveDevice* current_spi_slave = &current_device->SPIdetails.spi_slave[current_device->SPIdetails.slave_count - 1];
                            memset(current_spi_slave, 0, sizeof(SPISlaveDevice));

                            current_spi_slave->slave_name = strdup(slave_item->string);
                            cJSON* cs_id = cJSON_GetObjectItemCaseSensitive(slave_item, "cs_id");
                            if (cJSON_IsNumber(cs_id)) {
                                current_spi_slave->cs_id = cs_id->valueint;
                            }
                            cJSON* slave_scroll = cJSON_GetObjectItemCaseSensitive(slave_item, "scroll_path");
                            if (cJSON_IsString(slave_scroll)) {
                                current_spi_slave->scroll_path = strdup(slave_scroll->valuestring);
                            }
                            cJSON* is_scroll = cJSON_GetObjectItemCaseSensitive(slave_item, "is_scroll_path");
                            if (cJSON_IsBool(is_scroll)) {
                                current_spi_slave->is_scroll_path = cJSON_IsTrue(is_scroll);
                            }
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    return app_config;
}

#include <stdio.h>

// NOTE: This assumes all the necessary struct definitions from your previous prompts are available.

/**
 * @brief Prints the entire parsed application configuration to the console.
 * This is a utility function primarily used for debugging to verify that the
 * JSON configuration was parsed correctly.
 *
 * @param config A pointer to the populated AppConfig struct.
 */
void dev_print_config(const AppConfig* config) {
    if (!config) {
        printf("Configuration is NULL.\n");
        return;
    }

    printf("--- Stored Configuration ---\n");
    for (int i = 0; i < config->section_count; i++) {
        ConfigSection* s = &config->sections[i];
        printf("\nSection: [%s] (Backend: %s)\n", s->name, s->backend ? s->backend : "null");

        // Print overall memory ranges for the section
        if (s->overall_range_count > 0) {
            printf("  Overall Ranges:\n");
            for (int k = 0; k < s->overall_range_count; k++){
                printf("    - %s\n", s->overall_ranges[k]);
            }
        }

        // Print details for each device in the section
        for (int j = 0; j < s->device_count; j++) {
            DeviceModels* d = &s->devices[j];
            printf("  Device: %s\n", d->name);
            printf("    -> Scroll: %s\n", d->scroll_path ? d->scroll_path : "N/A");
            printf("    -> Ranges: %d\n", d->range_count);
            printf("    -> IRQ: %s\n", d->irq ? d->irq : "N/A");

            // --- Print I2C Slaves ---
            I2CDevices* i2c_devs = &d->I2Cdevices;
            if (i2c_devs->device_count > 0) {
                printf("    -> I2C Slaves:\n");
                for (int l = 0; l < i2c_devs->device_count; l++) {
                    I2CDevice* curr_i2c = &i2c_devs->i2cdevice[l];
                    printf("       - Name: %s\n", curr_i2c->slave_name);
                    printf("         -> Address: 0x%X\n", curr_i2c->address);
                    printf("         -> Is Scroll: %s\n", curr_i2c->_is_scroll ? "true" : "false");
                    printf("         -> Path: %s\n", curr_i2c->scroll_path ? curr_i2c->scroll_path : "N/A");
                }
            }

            // --- Print SPI Slaves (NEW) ---
            SPIDevices* spi_devs = &d->SPIdetails;
            if (spi_devs->slave_count > 0) {
                printf("    -> SPI Slaves:\n");
                for (int m = 0; m < spi_devs->slave_count; m++) {
                    SPISlaveDevice* curr_spi = &spi_devs->spi_slave[m];
                    printf("       - Name: %s\n", curr_spi->slave_name);
                    printf("         -> CS ID: %d\n", curr_spi->cs_id);
                    printf("         -> Is Scroll: %s\n", curr_spi->is_scroll_path ? "true" : "false");
                    printf("         -> Path: %s\n", curr_spi->scroll_path ? curr_spi->scroll_path : "N/A");
                }
            }
        }
    }
    printf("---------------------------\n");
}

static inline AppConfig* dev_parse_models(const char *s, ModelEntry *entries, int max_entries) {
    // Skip "dev=" prefix if present
    if (strncmp(s, "dev=", 4) == 0) {
        s += 4;
    }

    char* json_data = dev_read_json_file(s);
    if (!json_data){
		utils_die("Unable to parse the json");
	}

    // 1. Parse the JSON into our C structs
    AppConfig* config = dev_parse_json_configs(json_data);
    free(json_data); // We no longer need the original string

    if (config) {
        // 2. Use the configuration data
        dev_print_config(config);

        if (config->section_count > max_entries) {
            utils_die("number of registered device models are greater than the maximum limit in the dev.c");
        }

        for (int i=0; i < config->section_count; i++) {
            ConfigSection* s = &config->sections[i];

            //model name
            strncpy(entries[i].model, s->name, sizeof(entries[i].model));
            entries[i].model[sizeof(entries[i].model) - 1] = '\0';

            //model args
            entries[i].args = s;        //This contains the details of the devices
        }
        return config;  //Returning it instead of the count in case the user needs it...
    } else {
        // 3. Free all allocated memory in case of an error
		dev_free_config(config);
		return NULL;
	}
}

int dev_init(int argc, char ** argv) {
	char * dev_model_info = utils_get_arg("dev", argc, argv);

	if (dev_model_info) {
		// Regisgter IRQ listener for logging
	    qemu_plugin_register_irq_hook(dev_notify_irq, dev_irqret_hook);
		qemu_plugin_unimp_export_device((void *)&importer);
		io_logger = fopen("io.log", "w");

		if (start_ts.tv_sec == 0 && start_ts.tv_nsec == 0) {
			    clock_gettime(CLOCK_MONOTONIC, &start_ts);
		}
		ModelEntry entries[10];
		AppConfig* current_config = dev_parse_models(dev_model_info, entries, 10);

		for (int i = 0; i < current_config->section_count; i++) {
				DeviceModel * current = find_device_model(entries[i].model);
				if (!current) {
						utils_die("Device Model not found.");
				}
				current->init(entries[i].args);
    	}
	}

	return 0;
}
