#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Include the header file defining the required API prototypes
// (This file is assumed to be provided by the emulation environment)
#include "boardrunner/vio.h"

// --- Slave State and Data ---

// This is the buffer the slave firmware arms for transmission.
static const uint8_t tx_message[] = "****SPI - Two Boards communication based on Polling **** SPI Message ******** SPI Message ******** SPI Message ****";

// The firmware main.c defines BUFFERSIZE as (COUNTOF(aTxBuffer) - 1).
// sizeof(tx_message) is 98 (including the null terminator).
// (COUNTOF(aTxBuffer) - 1) is (98 - 1) = 97.
// The firmware explicitly sends 97 bytes, excluding the null terminator.
#define FIRMWARE_BUFFER_SIZE 97

// This buffer will store the bytes received from the master.
static uint8_t rx_buffer[FIRMWARE_BUFFER_SIZE];

// --- Model State Variables ---

// Tracks the current byte index within the transaction.
static uint16_t transfer_idx = 0;

// Tracks the SPI Chip Select (CS) state.
static bool is_chip_selected = false;

// Tracks if the one-and-only transaction has been completed.
// This is the key state variable to model the firmware's one-shot behavior.
static bool transaction_completed = false;


// --- SPI Slave API Implementations ---

/**
 * @brief Callback to inform the slave model of its Chip Select (CS) state.
 * @param level 0 for active (LOW), 1 for inactive (HIGH).
 */
void STM32F4_set_cs(int level) {
    if (level == 0) {
        // CS is asserted (active LOW).
        is_chip_selected = true;

        // Per SPI protocol (CPHA=0), a falling CS edge marks the
        // beginning of a new transaction. We reset the index.
        transfer_idx = 0;

        if (transaction_completed) {
            printf("SPI SLAVE: Chip Select ACTIVE. (Note: Firmware has already completed its one transaction and is in while(1)).\n");
        } else {
            printf("SPI SLAVE: Chip Select ACTIVE. Ready for transaction.\n");
        }
    } else {
        // CS is de-asserted (inactive HIGH).
        is_chip_selected = false;

        // If the transaction just finished (we sent all our bytes),
        // we mark it as completed. The firmware would now return
        // from HAL_SPI_TransmitReceive and enter while(1).
        if (transfer_idx >= FIRMWARE_BUFFER_SIZE && !transaction_completed) {
            transaction_completed = true;
            printf("SPI SLAVE: Chip Select INACTIVE. Transaction complete. Firmware model now in idle state.\n");

            // Optional: Log the received buffer for debugging
            // printf("SPI SLAVE: Received data: \n");
            // for(int i=0; i<FIRMWARE_BUFFER_SIZE; i++) {
            //     printf("0x%02X ", rx_buffer[i]);
            // }
            // printf("\n");
        } else {
             printf("SPI SLAVE: Chip Select INACTIVE.\n");
        }
    }
}

/**
 * @brief Callback to handle a full-duplex SPI data transfer (one byte).
 * This function is called by the emulator for each byte the
 * master clocks out.
 * @param data The 8-bit data byte sent by the master (MOSI).
 * @return The 8-bit data byte sent by the slave (MISO).
 */
uint32_t STM32F4_transfer(uint32_t data) {
    // If CS is not active, MISO is high-impedance (returns 0xFF).
    if (!is_chip_selected) {
        return 0xFF;
    }

    // If the one-shot transaction is already complete, the firmware is
    // in while(1) and the SPI peripheral is no longer armed.
    // It will return 0xFF (or 0x00, 0xFF is safer).
    if (transaction_completed) {
        printf("SPI SLAVE: Master sent 0x%02X, but transaction is complete. Replying 0xFF.\n", (uint8_t)data);
        return 0xFF;
    }

    // Check if we are still within the bounds of our one-shot transaction.
    if (transfer_idx < FIRMWARE_BUFFER_SIZE) {
        // Get the byte we need to send from our TX buffer.
        uint8_t byte_to_send = tx_message[transfer_idx];

        // Store the byte we received from the master.
        rx_buffer[transfer_idx] = (uint8_t)data;

        printf("SPI SLAVE: (Byte %u) Master sent 0x%02X, Slave replied with 0x%02X ('%c')\n",
               transfer_idx,
               (uint8_t)data,
               byte_to_send,
               (byte_to_send >= 32 && byte_to_send <= 126) ? byte_to_send : '.');

        // Move to the next byte index.
        transfer_idx++;

        return byte_to_send;
    } else {
        // Master is sending more bytes than the firmware was armed for (97).
        // The HAL function would have returned, so the peripheral is idle.
        printf("SPI SLAVE: Master sent extra byte 0x%02X. Transaction size exceeded. Replying 0xFF.\n", (uint8_t)data);
        return 0xFF;
    }
}
