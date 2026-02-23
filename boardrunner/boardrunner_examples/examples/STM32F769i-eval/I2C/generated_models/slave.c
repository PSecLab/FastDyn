#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * These headers would be provided by the emulation environment.
 * "utils.h" might contain logging functions (like t_printf).
 * the function pointer typedefs, and the 'enum i2c_event' definition.
 */
#include "utils.h"
#include "boardrunner/vio.h"

// --- Constants derived from the slave firmware ---

/**
 * @brief The 10-bit I2C address the slave will respond to.
 * Derived from #define I2C_ADDRESS 0x30F and hi2c1.Init.OwnAddress1 = 783.
 */
#define SLAVE_I2C_ADDRESS 0x30F

/**
 * @brief The hardcoded string the slave transmits to the master.
 * Derived from 'uint8_t aTxBuffer[]' in the slave firmware.
 */
static const uint8_t slave_tx_buffer_str[] = " ****I2C_TwoBoards communication based on Polling**** ****I2C_TwoBoards communication based on Polling**** ****I2C_TwoBoards communication based on Polling**** ";

/**
 * @brief Size of the communication buffers.
 * Derived from #define TXBUFFERSIZE (COUNTOF(aTxBuffer) - 1)
 */
#define BUFFER_SIZE (sizeof(slave_tx_buffer_str) - 1)


// --- Slave State Variables ---

// Buffer to store data received from the master.
static uint8_t slave_rx_buffer[BUFFER_SIZE];

// Index for writing into the buffer during a master-to-slave transfer.
static uint16_t slave_rx_idx = 0;

// Index for reading from the buffer during a slave-to-master transfer.
static uint16_t slave_tx_idx = 0;


// --- Slave Function Implementations ---

/**
 * @brief Slave's "receive" callback. (Slave SENDS data)
 *
 * This function is called by the I2C bus model when the master requests a
 * byte of data (Master-Read operation).
 * It retrieves the next byte from the slave's *transmit* buffer (the constant string).
 *
 * @return uint8_t The byte to be sent to the master.
 */
uint8_t STM32F4_receive() {
    if (slave_tx_idx < BUFFER_SIZE) {
        // Read the byte from the hardcoded transmit buffer
        uint8_t data_to_send = slave_tx_buffer_str[slave_tx_idx];
        slave_tx_idx++;
        // t_printf(T_S_DEBUG, "SLAVE: Sending byte %d: 0x%02X", slave_tx_idx, data_to_send);
        return data_to_send;
    }

    // Master is trying to read more data than the slave has.
    fprintf(stderr, "Error: Slave send buffer underrun!\n");
    return 0x00; // Send dummy data
}

/**
 * @brief Slave's "send" callback. (Slave RECEIVES data)
 *
 * This function is called by the I2C bus model when the master sends a
 * byte of data (Master-Write operation).
 * It stores the received byte in the slave's *receive* buffer.
 *
 * @param data The byte received from the master.
 * @retval int Returns 0 for ACK (success) or 1 for NACK (e.g., buffer full).
 */
int STM32F4_send(uint8_t data) {
    if (slave_rx_idx < BUFFER_SIZE) {
        slave_rx_buffer[slave_rx_idx++] = data;
        // t_printf(T_S_DEBUG, "SLAVE: Received byte %d: 0x%02X", slave_rx_idx, data);
        return 0; // Acknowledge the byte
    }

    // If the master sends more data than we have space for, NACK it.
    fprintf(stderr, "Error: Slave receive buffer overflow!\n");
    return 1; // NACK
}

/**
 * @brief Slave's "event" callback.
 *
 * This function is called by the I2C bus model on major bus events.
 * Based on the firmware logic (HAL_I2C_Slave_Receive followed by
 * HAL_I2C_Slave_Transmit), the slave performs two separate transactions.
 * We reset the appropriate index based on the transaction type.
 *
 * @param event An enum i2c_event representing the bus event.
 * @retval int Returns 0 for success.
 */
int STM32F4_event(enum i2c_event event) {
    switch (event) {
        case I2C_START_SEND:
            // Master is starting a WRITE (Slave will RECEIVE)
            // Reset the receive buffer index to prepare for new data.
            // t_printf(T_S_INFO, "SLAVE Event: I2C_START_SEND (Master Write). RX index reset.");
            slave_rx_idx = 0;
            break;

        case I2C_START_RECV:
            // Master is starting a READ (Slave will SEND)
            // Reset the transmit buffer index to send from the beginning.
            // t_printf(T_S_INFO, "SLAVE Event: I2C_START_RECV (Master Read). TX index reset.");
            slave_tx_idx = 0;
            break;

        case I2C_FINISH:
            // Transaction finished (STOP condition)
            // t_printf(T_S_INFO, "SLAVE Event: I2C_FINISH (STOP).");
            break;

        case I2C_NACK:
            // Master NACK'd a byte from the slave
            // t_printf(T_S_INFO, "SLAVE Event: I2C_NACK (Master NACK'd).");
            break;

        default:
            // t_printf(T_S_WARN, "SLAVE Event: Unknown event type.");
            break;
    }
    return 0; // Success
}