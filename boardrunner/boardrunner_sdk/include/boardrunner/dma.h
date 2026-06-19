#pragma once
#include <stdint.h>
#include <device.h>   // ConfigSection, etc.

// Function pointer types for DMA stream handlers
typedef void (*dma_request_handler_t)(void *opaque);
typedef void (*dma_request_data_handler_t)(void *opaque,
                                           const uint8_t *data,
                                           int len);

/**
 * @brief Called by a DMA model to register a no-payload stream handler.
 *
 * @param controller_id The DMA controller index (e.g. 1 for DMA1)
 * @param stream_id The DMA stream index (e.g. 4 for Stream4)
 * @param handler The callback function
 * @param opaque User data
 */
void api_dma_register_stream(int controller_id, int stream_id,
                             dma_request_handler_t handler,
                             void *opaque);

/**
 * @brief Called by a peripheral (e.g., ADC) to trigger a no-payload DMA request.
 *
 * @param controller_id The DMA controller index
 * @param stream_id The DMA stream index
 */
void api_dma_request(int controller_id, int stream_id);

/**
 * @brief Called by a DMA model to register a payload-carrying stream handler.
 *
 * @param controller_id The DMA controller index
 * @param stream_id The DMA stream index
 * @param handler The callback function receiving the payload
 * @param opaque User data
 */
void api_dma_register_stream_data(int controller_id, int stream_id,
                                  dma_request_data_handler_t handler,
                                  void *opaque);

/**
 * @brief Called by a source peripheral to trigger a DMA request with payload.
 *
 * @param controller_id The DMA controller index
 * @param stream_id The DMA stream index
 * @param target_addr The exact memory-mapped register address (e.g., SPI2_DR) to correlate logs
 * @param data Pointer to the payload bytes
 * @param len Length of the payload in bytes (max 8)
 * @return 0 on success, negative on error
 */
int api_dma_request_data(int controller_id, int stream_id, uint32_t target_addr, const uint8_t *data, int len);
