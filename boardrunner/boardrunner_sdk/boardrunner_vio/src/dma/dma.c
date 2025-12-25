#include <boardrunner/dma.h>

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