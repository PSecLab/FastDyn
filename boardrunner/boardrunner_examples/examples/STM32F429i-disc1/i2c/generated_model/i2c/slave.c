#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * These headers would be provided by the emulation environment.
 * "utils.h" might contain logging functions.
 * "devmodels_apis.h" must contain the definition for the SlaveDetails struct
 * and the function pointer typedefs for the callbacks.
 */
#include "utils.h"
#include "boardrunner/vio.h"

// --- Constants derived from the master firmware ---

/**
 * @brief The 10-bit I2C address the slave will respond to.
 */
#define SLAVE_I2C_ADDRESS 0x30F

/**
 * @brief Size of the communication buffer.
 */
#define BUFFER_SIZE 175

// --- Slave State Variables ---

// Buffer to store data received from the master, which will then be echoed back.
static uint8_t slave_buffer[BUFFER_SIZE];
// Index for writing into the buffer during a master-to-slave transfer.
static uint16_t slave_rx_idx = 0;
// Index for reading from the buffer during a slave-to-master transfer.
static uint16_t slave_tx_idx = 0;

// --- Slave Function Implementations ---

/**
 * @brief Slave's "receive" callback.
 * This function is called by the I2C bus model whenever the master requests a byte of data.
 * It retrieves the next byte from the local buffer to send back to the master.
 *
 * @note The logic and signature have been corrected. This function now correctly SENDS data.
 *
 * @return uint8_t The byte to be sent to the master.
 */
uint8_t STM32F4_receive() {
    if (slave_tx_idx < BUFFER_SIZE) {
        // Corrected: Read the byte first, then increment the index.
        uint8_t data_to_send = slave_buffer[slave_tx_idx];
        slave_tx_idx++;
        return data_to_send;
    }
    fprintf(stderr, "Error: Slave send buffer underrun!\n");
    return 0x00;
}
/**
 * @brief Slave's "send" callback.
 * This function is called by the I2C bus model whenever the master sends a byte of data.
 * It stores the received byte in a local buffer.
 *
 * @note The logic and signature have been corrected. This function now correctly RECEIVES data.
 *
 * @param data The byte received from the master.
 * @retval int Returns 0 for ACK (success) or 1 for NACK (e.g., buffer full).
 */
int STM32F4_send(uint8_t data) {
    if (slave_rx_idx < BUFFER_SIZE) {
        slave_buffer[slave_rx_idx++] = data;
        return 0; // Acknowledge the byte
    }
    // If the master sends more data than we have space for, NACK it.
    fprintf(stderr, "Error: Slave receive buffer overflow!\n");
    return 1;
}

/**
 * @brief Slave's "event" callback.
 * This function is called by the I2C bus model on major bus events like START or STOP.
 * We use this to reset the buffer pointers, preparing the slave for a new transaction.
 *
 * @param event An integer representing the bus event (the exact values depend on the bus model).
 * @retval int Returns 0 for success.
 */
int STM32F4_event(enum i2c_event event) {
    // The master firmware completes a full transmit sequence, then initiates a
    // new receive sequence. Resetting both buffer indices on any new event
    // correctly prepares the slave to first receive the whole message, and then
    // send it back from the beginning.
    // Optional: print a human-readable name
    switch (event) {
        case I2C_START_RECV: printf("Event: I2C_START_RECV\n"); break;
        case I2C_START_SEND: printf("Event: I2C_START_SEND\n"); break;
        case I2C_FINISH:     printf("Event: I2C_FINISH\n"); break;
        case I2C_NACK:       printf("Event: I2C_NACK\n"); break;
        default:             printf("Unknown event\n"); break;
    }
    slave_rx_idx = 0;
    slave_tx_idx = 0;

    // This debug print can be removed in production.
    // printf("I2C slave event triggered: Buffer indices have been reset.\n");

    return 0;
}