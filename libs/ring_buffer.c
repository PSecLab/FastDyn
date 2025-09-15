/**
 * @file ring_buffer.c
 * @brief Implementation of a thread-safe circular (ring) buffer for byte streams.
 */

#include "ring_buffer.h"

void ring_buffer_init(RingBuffer *rb, uint8_t *storage, size_t size) {
    rb->buffer = storage;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    pthread_mutex_init(&rb->lock, NULL);
}

void ring_buffer_destroy(RingBuffer *rb) {
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
    size_t count;
    if (rb->head >= rb->tail) {
        count = rb->head - rb->tail;
    } else {
        count = rb->size - (rb->tail - rb->head);
    }
    pthread_mutex_unlock(&rb->lock);
    return count;
}
