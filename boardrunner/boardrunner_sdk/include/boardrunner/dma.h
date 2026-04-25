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
 * Used by streams whose source data does not need to be carried via the
 * request (e.g. memory-to-peripheral or memory-to-memory DMA, or when the
 * DMA model can derive its own data without help from the source).
 */
void api_dma_register_stream(int stream_id,
                             dma_request_handler_t handler,
                             void *opaque);

/**
 * @brief Called by a peripheral (e.g., ADC) to trigger a no-payload DMA request.
 *
 * Dispatch is deferred to the next translation-block boundary so the
 * registered handler runs outside the caller's MMIO callback context.
 */
void api_dma_request(int stream_id);

/**
 * @brief Called by a DMA model to register a payload-carrying stream handler.
 *
 * Used for peripheral->memory DMA in compositional model splits where the
 * DMA model cannot legally read the source peripheral's data register
 * (re-entrancy guard) and the source peripheral therefore must hand its
 * sample bytes to the DMA via the request. The framework guarantees FIFO
 * order: handlers receive payloads in the same order they were enqueued.
 */
void api_dma_register_stream_data(int stream_id,
                                  dma_request_data_handler_t handler,
                                  void *opaque);

/**
 * @brief Called by a source peripheral to trigger a DMA request with payload.
 *
 * The framework copies `len` bytes from `data` into an internal queue and
 * later invokes the registered payload handler with the same bytes,
 * outside any MMIO callback context. Returns 0 on success, negative on
 * error (invalid stream, no handler registered, payload too large, or
 * queue full).
 */
int api_dma_request_data(int stream_id, const uint8_t *data, int len);
