/**
 * @file ring_buffer.c
 * @brief Implementation of a thread-safe circular (ring) buffer for byte streams.
 */

#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int ring_buffer_init(RingBuffer *rb, size_t size) {
    rb->buffer = malloc(size);
    if (!rb->buffer) {
        // Handle memory allocation failure
        perror("Failed to allocate ring buffer");
        return -1;
    }
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    if (pthread_mutex_init(&rb->lock, NULL) != 0) {
        free(rb->buffer);
        return -1;
    }
    return 0;
}

void ring_buffer_destroy(RingBuffer *rb) {
    free(rb->buffer);
    rb->buffer = NULL;
    rb->size = 0;
    rb->head = 0;
    rb->tail = 0;
    pthread_mutex_destroy(&rb->lock);
}

bool ring_buffer_put(RingBuffer *rb, uint8_t data) {
    pthread_mutex_lock(&rb->lock);
    size_t next = (rb->head + 1) % rb->size;
    if (next == rb->tail) {
        pthread_mutex_unlock(&rb->lock);
        return false;
    }
    rb->buffer[rb->head] = data;
    rb->head = next;
    pthread_mutex_unlock(&rb->lock);
    return true;
}

bool ring_buffer_get(RingBuffer *rb, uint8_t *data) {
    pthread_mutex_lock(&rb->lock);
    if (rb->head == rb->tail) {
        pthread_mutex_unlock(&rb->lock);
        return false;
    }
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    pthread_mutex_unlock(&rb->lock);
    return true;
}

bool ring_buffer_is_empty(RingBuffer *rb) {
    pthread_mutex_lock(&rb->lock);
    bool result = (rb->head == rb->tail);
    pthread_mutex_unlock(&rb->lock);
    return result;
}

bool ring_buffer_is_full(RingBuffer *rb) {
    pthread_mutex_lock(&rb->lock);
    bool result = ((rb->head + 1) % rb->size) == rb->tail;
    pthread_mutex_unlock(&rb->lock);
    return result;
}

size_t ring_buffer_count(RingBuffer *rb) {
    pthread_mutex_lock(&rb->lock);
    size_t count = (rb->head + rb->size - rb->tail) % rb->size;
    pthread_mutex_unlock(&rb->lock);
    return count;
}
