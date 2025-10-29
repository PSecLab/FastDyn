#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utils.h>
#include <devmodels_apis.h>

//Returns the pseudo terminal handler
int api_pty_fd_gen(void) {
    const char *pty_path = "/tmp/usart1_pty";
    int fd = open(pty_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("Unable to open PTY");
    }
    return fd;
}

// Sends a single byte to the pseudo-terminal fd (blocking write)
void api_pty_write_req(int fd, uint8_t value) {
        if (write(fd, &value, 1) < 0) {
            perror("USART1-ERROR: Write() to PTY Failed");
        }
}

/**
 * @brief Attempts to read a single byte from the PTY file descriptor
 * in a non-blocking manner.
 * @param fd The file descriptor for the PTY.
 * @param buff A pointer to a uint8_t where the read byte will be stored.
 * @return 1 on success (a byte was read and stored in out_byte).
 * 0 if no data was available to read.
 * -1 on a critical error.
 */
int api_pty_read_nonblock(int fd, uint8_t *buff) {
    ssize_t n = read(fd, buff, 1);

    if (n == 1) {
        // Success: We read exactly one byte.
        return 1;
    } else if (n == -1) {
        // An error occurred. Check if it was because the buffer was empty.
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        } else {
            perror("Critical PTY read error");
            return -1;
        }
    }
    return -1;
}

//----------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------I2C APIS-----------------------------------------------------------------------

/**
 * @brief Initializes an I2C bus by parsing slave device configuration.
 *
 * @param model_info The configuration section containing device information.
 * @return An initialized I2CBus struct. If initialization fails, the
 * returned struct will be empty (num_slaves will be 0).
 */
I2CBus api_i2c_init_bus(ConfigSection* model_info) {
    I2CBus bus = {0};
    bool success;
    success = i2c_slave_device_parse(&bus, model_info);
    if (!success) {
        utils_die("Unable to parse the slave devices attached to I2C");
    }
    return bus;
}

/**
 * @brief Starts an I2C transaction with a specific slave device.
 *
 * This function serves as the public API to initiate a transaction. It finds the
 * target slave on the bus using its 7-bit address and signals the start of
 * either a master-send (write) or master-receive (read) operation.
 *
 * @param bus       A pointer to the I2CBus instance being operated on.
 * @param address   The 7-bit address of the target slave device.
 * @param is_recv   The direction of the transfer: 'true' for a read operation
 * (master receives), 'false' for a write operation (master sends).
 * @return          0 on success (the slave acknowledged the address), or a non-zero
 * value on failure (slave not found or did not acknowledge).
 */
int api_i2c_start_transfer(I2CBus* bus, uint8_t address, bool is_recv) {
    return i2c_do_start_transfer(bus, address, is_recv
                                               ? I2C_START_RECV
                                               : I2C_START_SEND);
}
int api_i2c_start_transfer_10bit(I2CBus* bus, uint16_t address, bool is_recv) {
    return i2c_do_start_transfer_10bit(bus, address, is_recv
                                               ? I2C_START_RECV
                                               : I2C_START_SEND);
}
/**
 * @brief Ends the current I2C transaction and resets the bus's active device.
 *
 * This function should be called after a STOP condition. It notifies the
 * currently active slave that the transfer is finished and then sets the
 * bus's current_dev pointer to NULL, making the bus ready for a new transaction.
 *
 * @param bus A pointer to the I2CBus structure.
 */
void api_i2c_end_transfer(I2CBus* bus) {
    if (bus->current_dev) {
        (void)bus->current_dev->event(I2C_FINISH);
        bus->current_dev = NULL;
    }
}

/**
 * @brief Sends a single data byte to the currently active slave device.
 * @param bus  A pointer to the I2CBus instance.
 * @param data The byte of data to send.
 * @return 0 on success (slave ACKed), or -1 on failure (slave NACKed or no active device).
 */
int api_i2c_send(I2CBus *bus, uint8_t data)
{
    // 1. Check if a transaction is active by checking the pointer itself.
    if (!bus->current_dev) {
        return -1; // No active device to send to.
    }

    // 2. Call the slave's send function using the correct syntax ('->').
    //    Pass the ADDRESS of the data to match the uint8_t* signature.
    int ret = bus->current_dev->send(data);

    // 3. Return 0 for success (if ret is 0), or -1 for failure (if ret is non-zero).
    return (ret == 0) ? 0 : -1;
}

/**
 * @brief Receives a single data byte from the currently active slave device.
 * @param bus A pointer to the I2CBus instance.
 * @return The byte of data received from the slave. Returns 0xFF if no
 * transaction is active.
 */
uint8_t api_i2c_recv(I2CBus *bus)
{
    // Check if a transaction is active.
    if (bus->current_dev) {
        // Call the slave's recv function and return the byte.
        return bus->current_dev->recv();
    }

    // If no device is active, return a default "bus high" value.
    return 0xFF;
}

//----------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------SPI APIS-----------------------------------------------------------------------
//SPI API functions definitions -- exposed to SPI Model writer
SPIBus api_spi_init_bus(ConfigSection* model_info);             //takes the user configuration for attached slaves and creates a bus with slaves attached
uint32_t api_spi_transfer(SPIBus *bus, uint32_t val);           //transfer the data to all the slaves and calls spi_transfer_raw_default for each slave
void api_spi_set_cs(SPIBus *bus, int cs_id, int level);
//End of SPI API funcitons definitions
/**
 * @brief Initializes an SPI bus by parsing slave device configuration.
 *
 * @param model_info The configuration section containing device information.
 * @return An initialized SPIBus struct. If initialization fails, the
 * returned struct will be empty (num_slaves will be 0).
 */
SPIBus api_spi_init_bus(ConfigSection* model_info) {
    SPIBus bus = {0};
    bool success;
    success = spi_slave_device_parse(&bus, model_info); //parses all the attached slave devices
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
        // Safety check for null bus pointer
        return;
    }

    // Determine the new enable state (1 for active, 0 for inactive)
    // This assumes an active-low chip select, which is standard.
    int new_enable_state = (level == 0) ? 1 : 0;

    // Loop through all slaves registered on the bus
    for (int i = 0; i < bus->Slaves.num_slaves; i++) {
        SpiSlaveDetails* current_slave = &bus->Slaves.slave[i];

        // Check if this slave is attached to the target CS line
        if (current_slave->cs_id == cs_id) {

            // 1. Update the bus's record of this slave's state
            current_slave->cs_enable = new_enable_state;

            // 2. Notify the slave model by calling its callback, if it exists
            if (current_slave->set_cs) {
                current_slave->set_cs(level);
            }
        }
    }
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
        // Safety check, return idle value
        return ret_val;
    }
    printf("Current value to be written\n %d", val);

    // Find the currently selected slave
    for (int i = 0; i < bus->Slaves.num_slaves; i++) {
        SpiSlaveDetails* current_slave = &bus->Slaves.slave[i];

        // Check if this slave is enabled (active)
        if (current_slave->cs_enable == 1) {

            // Found the active slave. Call its transfer function, if it exists.
            if (current_slave->transfer) {
                ret_val = current_slave->transfer(val);
                return current_slave->transfer(val);
            } else {
                // Slave is active but has no transfer function.
                // This is a configuration error, but we'll return idle.
                fprintf(stderr, "SPI Error: Slave '%s' is active but has no transfer function.\n",
                        current_slave->name ? current_slave->name : "unknown");
                return ret_val;
            }
        }
    }
    printf("Current value to be returned\n %d", ret_val);

    // If no slave was found to be active, return the idle bus value
    return ret_val;
}

#define MAX_DMA_STREAMS 16

// A struct to hold the registered callback for each stream
typedef struct {
    dma_request_handler_t handle_request_cb;
    void* opaque;
} RegisteredStream;

// The central registry for all DMA streams
static RegisteredStream registered_streams[MAX_DMA_STREAMS];

/**
 * @brief Definition of the registration function.
 */
void api_dma_register_stream(int stream_id, dma_request_handler_t handler, void *opaque) {
    if (stream_id >= 0 && stream_id < MAX_DMA_STREAMS) {
        registered_streams[stream_id].handle_request_cb = handler;
        registered_streams[stream_id].opaque = opaque;
    }
}

/**
 * @brief Definition of the request function.
 */
void api_dma_request(int stream_id) {
    if (stream_id >= 0 && stream_id < MAX_DMA_STREAMS && registered_streams[stream_id].handle_request_cb) {
        // Look up and call the registered function for the given stream
        registered_streams[stream_id].handle_request_cb(registered_streams[stream_id].opaque);
    }
}