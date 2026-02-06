#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "boardrunner/vio.h"

// --- Slave State and Data ---

static const uint8_t tx_message[] = "****SPI - Two Boards communication based on Polling **** SPI Message ******** SPI Message ******** SPI Message ****";
#define BUFFER_SIZE sizeof(tx_message)

static uint8_t rx_buffer[BUFFER_SIZE];
static uint16_t transfer_idx = 0;
static bool is_chip_selected = false;

// --- Corrected Slave Functions ---

/**
 * @brief Callback to inform the slave of its Chip Select (CS) state.
 */
void STM32F4_set_cs(int level) {
    if (level == 0) { // Active-low CS
        is_chip_selected = true;
        // CORRECTED: Do NOT reset the index here.
        // This allows the transaction to continue across multiple CS toggles.
        // transfer_idx = 0;
        printf("SPI SLAVE: Chip Select ACTIVE. Ready for transaction.\n");
    } else {
        is_chip_selected = false;
        printf("SPI SLAVE: Chip Select INACTIVE. Transaction ended.\n");
        // Optional: You might want to reset the index or print the buffer here
        // if a de-assertion truly means the end of a full message.
    }
}

/**
 * @brief Callback to handle a full-duplex SPI data transfer.
 */
uint32_t STM32F4_transfer(uint32_t data) {
    if (!is_chip_selected) {
        return 0xFF; // MISO is high-impedance when not selected
    }

    // Reset index if it goes out of bounds to loop the message
    if (transfer_idx >= BUFFER_SIZE) {
        transfer_idx = 0;
    }

    uint8_t byte_to_send = tx_message[transfer_idx];
    rx_buffer[transfer_idx] = (uint8_t)data;

    printf("SPI SLAVE: Master sent 0x%02X, Slave replied with 0x%02X ('%c')\n",
           (uint8_t)data, byte_to_send, byte_to_send > 31 ? byte_to_send : '.');

    transfer_idx++;

    return byte_to_send;
}