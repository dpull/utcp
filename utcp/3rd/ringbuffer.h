﻿#include <inttypes.h>

/**
 * @file
 * Prototypes and structures for the ring buffer module.
 */

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * The size of a ring buffer.
 * Due to the design only <tt> RING_BUFFER_SIZE-1 </tt> items
 * can be contained in the buffer.
 * The buffer size must be a power of two.
*/
#define RING_BUFFER_SIZE 256

#if (RING_BUFFER_SIZE & (RING_BUFFER_SIZE - 1)) != 0
#error "RING_BUFFER_SIZE must be a power of two"
#endif

/**
 * The type which is used to hold the size
 * and the indicies of the buffer.
 * Must be able to fit \c RING_BUFFER_SIZE .
 */
typedef uint16_t ring_buffer_size_t;
typedef uint32_t ring_buffer_elem_t;

/**
 * Used as a modulo operator
 * as <tt> a % b = (a & (b − 1)) </tt>
 * where \c a is a positive index in the buffer and
 * \c b is the (power of two) size of the buffer.
 */
#define RING_BUFFER_MASK (RING_BUFFER_SIZE-1)

/**
 * Simplifies the use of <tt>struct ring_buffer_t</tt>.
 */
typedef struct ring_buffer_t ring_buffer_t;

/**
 * Structure which holds a ring buffer.
 * The buffer contains a buffer array
 * as well as metadata for the ring buffer.
 */
struct ring_buffer_t {
  /** Buffer memory. */
  ring_buffer_elem_t buffer[RING_BUFFER_SIZE];
  /** Index of tail. */
  ring_buffer_size_t tail_index;
  /** Index of head. */
  ring_buffer_size_t head_index;
};

/**
 * Initializes the ring buffer pointed to by <em>buffer</em>.
 * This function can also be used to empty/reset the buffer.
 * @param buffer The ring buffer to initialize.
 */
void ring_buffer_init(ring_buffer_t *buffer);

/**
 * Adds a byte to a ring buffer.
 * @param buffer The buffer in which the data should be placed.
 * @param data The byte to place.
 */
void ring_buffer_queue(ring_buffer_t* buffer, ring_buffer_elem_t data);

/**
 * Returns the oldest byte in a ring buffer.
 * @param buffer The buffer from which the data should be returned.
 * @param data A pointer to the location at which the data should be placed.
 * @return 1 if data was returned; 0 otherwise.
 */
uint8_t ring_buffer_dequeue(ring_buffer_t* buffer, ring_buffer_elem_t* data);

/**
 * Returns whether a ring buffer is empty.
 * @param buffer The buffer for which it should be returned whether it is empty.
 * @return 1 if empty; 0 otherwise.
 */
inline uint8_t ring_buffer_is_empty(ring_buffer_t *buffer) {
  return (buffer->head_index == buffer->tail_index);
}

/**
 * Returns whether a ring buffer is full.
 * @param buffer The buffer for which it should be returned whether it is full.
 * @return 1 if full; 0 otherwise.
 */
inline uint8_t ring_buffer_is_full(ring_buffer_t *buffer) {
  return ((buffer->head_index - buffer->tail_index) & RING_BUFFER_MASK) == RING_BUFFER_MASK;
}

/**
 * Returns the number of items in a ring buffer.
 * @param buffer The buffer for which the number of items should be returned.
 * @return The number of items in the ring buffer.
 */
inline ring_buffer_size_t ring_buffer_num_items(ring_buffer_t *buffer) {
  return ((buffer->head_index - buffer->tail_index) & RING_BUFFER_MASK);
}

#ifdef __cplusplus
}
#endif

#endif /* RINGBUFFER_H */
