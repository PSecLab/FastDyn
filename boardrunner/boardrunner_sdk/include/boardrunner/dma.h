#pragma once
#include <stdint.h>
#include <device.h>   // ConfigSection, etc.

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
