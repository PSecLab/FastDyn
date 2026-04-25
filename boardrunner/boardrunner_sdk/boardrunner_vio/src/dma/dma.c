#include <boardrunner/dma.h>
#include <string.h>

#define MAX_DMA_STREAMS         16
#define MAX_PAYLOAD_BYTES       8

/* No-payload stream registry */
typedef struct {
    dma_request_handler_t handle_request_cb;
    void* opaque;
} RegisteredStream;

static RegisteredStream registered_streams[MAX_DMA_STREAMS];

/* Payload-carrying stream registry */
typedef struct {
    dma_request_data_handler_t handler;
    void *opaque;
} PayloadStream;

static PayloadStream payload_streams[MAX_DMA_STREAMS];

void api_dma_register_stream(int stream_id,
                             dma_request_handler_t handler,
                             void *opaque) {
    if (stream_id < 0 || stream_id >= MAX_DMA_STREAMS) {
        return;
    }
    registered_streams[stream_id].handle_request_cb = handler;
    registered_streams[stream_id].opaque = opaque;
}

void api_dma_request(int stream_id) {
    if (stream_id < 0 || stream_id >= MAX_DMA_STREAMS) {
        return;
    }
    if (registered_streams[stream_id].handle_request_cb) {
        registered_streams[stream_id].handle_request_cb(
            registered_streams[stream_id].opaque);
    }
}

void api_dma_register_stream_data(int stream_id,
                                  dma_request_data_handler_t handler,
                                  void *opaque) {
    if (stream_id < 0 || stream_id >= MAX_DMA_STREAMS) {
        return;
    }
    payload_streams[stream_id].handler = handler;
    payload_streams[stream_id].opaque  = opaque;
}

int api_dma_request_data(int stream_id, const uint8_t *data, int len) {
    if (stream_id < 0 || stream_id >= MAX_DMA_STREAMS) {
        return -1;
    }
    if (len < 0 || len > MAX_PAYLOAD_BYTES) {
        return -1;
    }
    if (!payload_streams[stream_id].handler) {
        return -1;
    }
    payload_streams[stream_id].handler(payload_streams[stream_id].opaque,
                                       data, len);
    return 0;
}
