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
typedef int (*SlaveSendFunc)(uint8_t data);
typedef uint8_t (*SlaveRecvFunc)();
typedef int (*SlaveEventFunc)(enum i2c_event event);

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

bool i2c_scan_bus(I2CBus *bus, uint8_t address, SlaveDetails **found_dev);
int i2c_do_start_transfer(I2CBus *bus, uint8_t address, enum i2c_event event);
int i2c_do_start_transfer_10bit(I2CBus *bus, uint16_t address, enum i2c_event event);
bool i2c_slave_device_parse(I2CBus* bus, ConfigSection* model_info);

I2CBus api_i2c_init_bus(ConfigSection* model_info);
int api_i2c_start_transfer(I2CBus* bus, uint8_t address, bool is_recv);
int api_i2c_start_transfer_10bit(I2CBus* bus, uint16_t address, bool is_recv);
void api_i2c_end_transfer(I2CBus* bus);
int api_i2c_send(I2CBus *bus, uint8_t data);
uint8_t api_i2c_recv(I2CBus *bus);