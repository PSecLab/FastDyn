#pragma once
#include <boardrunner/i2c.h>

// Internal helpers (not exposed to model authors)
bool i2c_scan_bus(I2CBus *bus, uint8_t address, SlaveDetails **found_dev);
int  i2c_do_start_transfer(I2CBus *bus, uint8_t address, enum i2c_event event);
int  i2c_do_start_transfer_10bit(I2CBus *bus, uint16_t address, enum i2c_event event);
bool i2c_slave_device_parse(I2CBus* bus, ConfigSection* model_info);
