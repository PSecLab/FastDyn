#pragma once
#include <boardrunner/spi.h>
#include <device.h>

//Warn: Do we need the following function? Not defined anywhere
// uint32_t spi_transfer_raw_default(SlaveDetails curr_slave, uint32_t value); //For passed slave, check cs_enable and based on that sends the value to slave.

bool spi_slave_device_parse(SPIBus* bus, ConfigSection* model_info);
