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