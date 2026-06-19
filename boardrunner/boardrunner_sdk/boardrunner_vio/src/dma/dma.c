#include <boardrunner/dma.h>
#include <string.h>
#include <device.h>
#include <stdio.h>

#define MAX_DMA_CONTROLLERS     4
#define MAX_DMA_STREAMS         16
#define MAX_PAYLOAD_BYTES       8

/* No-payload stream registry */
typedef struct {
    dma_request_handler_t handle_request_cb;
    void* opaque;
} RegisteredStream;

static RegisteredStream registered_streams[MAX_DMA_CONTROLLERS][MAX_DMA_STREAMS];

/* Payload-carrying stream registry */
typedef struct {
    dma_request_data_handler_t handler;
    void *opaque;
} PayloadStream;

static PayloadStream payload_streams[MAX_DMA_CONTROLLERS][MAX_DMA_STREAMS];

void api_dma_register_stream(int controller_id, int stream_id,
                             dma_request_handler_t handler,
                             void *opaque) {
    if (controller_id < 0 || controller_id >= MAX_DMA_CONTROLLERS) return;
    if (stream_id < 0 || stream_id >= MAX_DMA_STREAMS) {
        return;
    }
    registered_streams[controller_id][stream_id].handle_request_cb = handler;
    registered_streams[controller_id][stream_id].opaque = opaque;
}

void api_dma_request(int controller_id, int stream_id) {
    if (controller_id < 0 || controller_id >= MAX_DMA_CONTROLLERS) return;
    if (stream_id < 0 || stream_id >= MAX_DMA_STREAMS) {
        return;
    }
    if (registered_streams[controller_id][stream_id].handle_request_cb) {
        registered_streams[controller_id][stream_id].handle_request_cb(
            registered_streams[controller_id][stream_id].opaque);
    }
}

void api_dma_register_stream_data(int controller_id, int stream_id,
                                  dma_request_data_handler_t handler,
                                  void *opaque) {
    if (controller_id < 0 || controller_id >= MAX_DMA_CONTROLLERS) return;
    if (stream_id < 0 || stream_id >= MAX_DMA_STREAMS) {
        return;
    }
    payload_streams[controller_id][stream_id].handler = handler;
    payload_streams[controller_id][stream_id].opaque  = opaque;
}

int api_dma_request_data(int controller_id, int stream_id, uint32_t target_addr, const uint8_t *data, int len) {
    if (controller_id < 0 || controller_id >= MAX_DMA_CONTROLLERS) return -1;
    if (stream_id < 0 || stream_id >= MAX_DMA_STREAMS) {
        return -1;
    }
    if (len < 0 || len > MAX_PAYLOAD_BYTES) {
        return -1;
    }
    if (!payload_streams[controller_id][stream_id].handler) {
        return -1;
    }

    char dump_buf[128];
    int pos = snprintf(dump_buf, sizeof(dump_buf),
                       "DMA%d Stream%d (Periph: 0x%08X) Transferred %d bytes. First %d bytes:",
                       controller_id, stream_id, target_addr, len, len);
    for (int i = 0; i < len && i < 8; i++) {
        pos += snprintf(dump_buf + pos, sizeof(dump_buf) - pos, " %02X", data[i]);
    }
    snprintf(dump_buf + pos, sizeof(dump_buf) - pos, "\n");
    printf("%s", dump_buf);

    static int memory_log_initialized = 0;
    FILE *f;
    if (!memory_log_initialized) {
        f = fopen("memory.log", "w");
        memory_log_initialized = 1;
    } else {
        f = fopen("memory.log", "a");
    }

    if (f) {
        fprintf(f, "%s", dump_buf);
        fclose(f);
    }

    payload_streams[controller_id][stream_id].handler(payload_streams[controller_id][stream_id].opaque,
                                       data, len);
    return 0;
}
