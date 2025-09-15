/**
 * @file ring_buffer.h
 * @brief Thread-safe circular (ring) buffer for byte streams.
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/**
 * @struct RingBuffer
 * @brief Thread-safe circular buffer for bytes.
 */
typedef struct {
    uint8_t *buffer;        /**< Pointer to buffer storage */
    size_t head;            /**< Index of the next write position */
    size_t tail;            /**< Index of the next read position */
    size_t size;            /**< Total capacity of the buffer */
    pthread_mutex_t lock;   /**< Mutex for thread safety */
} RingBuffer;

/**
 * @brief Initialize a ring buffer.
 *
 * @param rb Pointer to the RingBuffer structure.
 * @param storage Pointer to user-provided storage array.
 * @param size Size of the storage array in bytes.
 */
void ring_buffer_init(RingBuffer *rb, uint8_t *storage, size_t size);

/**
 * @brief Add a byte to the buffer.
 *
 * @param rb Pointer to the RingBuffer structure.
 * @param data Byte to add.
 * @return true if successfully added, false if buffer is full.
 */
bool ring_buffer_put(RingBuffer *rb, uint8_t data);

/**
 * @brief Retrieve a byte from the buffer.
 *
 * @param rb Pointer to the RingBuffer structure.
 * @param data Pointer to variable where retrieved byte will be stored.
 * @return true if a byte was retrieved, false if buffer is empty.
 */
bool ring_buffer_get(RingBuffer *rb, uint8_t *data);

/**
 * @brief Check if the buffer is empty.
 *
 * @param rb Pointer to the RingBuffer structure.
 * @return true if buffer is empty, false otherwise.
 */
bool ring_buffer_is_empty(RingBuffer *rb);

/**
 * @brief Check if the buffer is full.
 *
 * @param rb Pointer to the RingBuffer structure.
 * @return true if buffer is full, false otherwise.
 */
bool ring_buffer_is_full(RingBuffer *rb);

/**
 * @brief Get the number of bytes currently stored in the buffer.
 *
 * @param rb Pointer to the RingBuffer structure.
 * @return Number of stored bytes.
 */
size_t ring_buffer_count(RingBuffer *rb);

#endif /* RING_BUFFER_H */