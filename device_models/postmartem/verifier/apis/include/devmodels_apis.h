#include <stdio.h>
#include <device.h>

int api_pty_fd_gen(void);
void api_pty_write_req(int fd, uint8_t value);
int api_pty_read_nonblock(int fd, uint8_t *buff);


enum i2c_event {
    I2C_START_RECV,
    I2C_START_SEND,
    I2C_FINISH,
    I2C_NACK /* Masker NACKed a receive byte.  */
};

// Typedefs for the slave device functions
//I2C
typedef int (*SlaveSendFunc)(uint8_t data);
typedef uint8_t (*SlaveRecvFunc)();
typedef int (*SlaveEventFunc)(enum i2c_event event);
//SPI
typedef uint32_t (*SlaveTransferFunc)(uint32_t value);
typedef void (*SlaveSetcsFunc)(int level);

// --- I2C Bus struct definitions here ---
typedef struct {
    char* name; //Name of the slave
    int address;    //address for which the slave will be registered
    SlaveSendFunc send; //Call this function when you want to send data and get ack from the slave device.
    SlaveRecvFunc recv; //Call this function when you want to receive data from the slave device.
    SlaveEventFunc event; //Call this function when you want to start transmission
} SlaveDetails;

typedef struct {
    int num_slaves;
    SlaveDetails* slave; //Dynamic array of slaves
} SlaveList;

typedef struct {
    SlaveList Slaves;  //Constant once registered on i2c_init_bus
    SlaveDetails* current_dev;//Current devices show the current devices being used for the transaction
    uint8_t saved_address; //saved_address is the address initiated for the transaction by the master.
} I2CBus;
// --- End of struct definitions ---

//I2C internal functions definitions
bool i2c_scan_bus(I2CBus *bus, uint8_t address, SlaveDetails **found_dev);
int i2c_do_start_transfer(I2CBus *bus, uint8_t address, enum i2c_event event);
int i2c_do_start_transfer_10bit(I2CBus *bus, uint16_t address, enum i2c_event event);
bool i2c_slave_device_parse(I2CBus* bus, ConfigSection* model_info);
//end of I2C internal functions definitions

//I2C API functions definitions
I2CBus api_i2c_init_bus(ConfigSection* model_info);
int api_i2c_start_transfer(I2CBus* bus, uint8_t address, bool is_recv);
int api_i2c_start_transfer_10bit(I2CBus* bus, uint16_t address, bool is_recv);
void api_i2c_end_transfer(I2CBus* bus);
int api_i2c_send(I2CBus *bus, uint8_t data);
uint8_t api_i2c_recv(I2CBus *bus);
//end of API functions definition

// --- SPI Bus struct definitions here ---
#define NUM_CS_LINES 4          //USER can change the max number based on the requirement

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

//SPI internal functions
uint32_t spi_transfer_raw_default(SlaveDetails curr_slave, uint32_t value); //For passed slave, check cs_enable and based on that sends the value to slave.
bool spi_slave_device_parse(SPIBus* bus, ConfigSection* model_info);
//End of SPI internal functions

//SPI API functions definitions -- exposed to SPI Model writer
SPIBus api_spi_init_bus(ConfigSection* model_info);             //takes the user configuration for attached slaves and creates a bus with slaves attached
uint32_t api_spi_transfer(SPIBus *bus, uint32_t val);           //transfer the data to all the slaves and calls spi_transfer_raw_default for each slave
void api_spi_set_cs(SPIBus *bus, int cs_id, int level);
//End of SPI API funcitons definitions


// --- DMA Dispatcher API ---

// Define the function pointer type for the DMA's request handler
typedef void (*dma_request_handler_t)(void *opaque);

/**
 * @brief Called by a DMA model to register its stream with the dispatcher.
 */
void api_dma_register_stream(int stream_id, dma_request_handler_t handler, void *opaque);

/**
 * @brief Called by a peripheral (e.g., ADC) to trigger a DMA request.
 */
void api_dma_request(int stream_id);
