#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <device.h>


#define NUM_CS_LINES 4          //USER can change the max number based on the requirement

typedef uint32_t (*SlaveTransferFunc)(uint32_t value);
typedef void (*SlaveSetcsFunc)(int level);

typedef struct {
    char* name;                 //Name of the slave
    int cs_id;                  //Chip select id for which the device will be registered
    int cs_enable;              //is the current chip enable
    SlaveTransferFunc transfer; //transfer function used to send and receive the data to/from slave
    SlaveSetcsFunc set_cs;      //function to tell the slave it is selected
} SpiSlaveDetails;

typedef struct {
    int num_slaves;
    SpiSlaveDetails* slave; //Dynamic array of slaves
} SpiSlaveList;

typedef struct {
    SpiSlaveList Slaves;  //Registered on spi_init_bus
} SPIBus;
// --- End of struct definitions ---

//SPI API functions definitions -- exposed to SPI Model writer
SPIBus api_spi_init_bus(ConfigSection* model_info);             //takes the user configuration for attached slaves and creates a bus with slaves attached
uint32_t api_spi_transfer(SPIBus *bus, uint32_t val);           //transfer the data to all the slaves and calls spi_transfer_raw_default for each slave
void api_spi_set_cs(SPIBus *bus, int cs_id, int level);
//End of SPI API funcitons definitions