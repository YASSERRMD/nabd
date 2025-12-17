/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Public API
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#ifndef NABD_H
#define NABD_H

#include "types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Lifecycle Functions
 * ============================================================================
 */

/**
 * Open or create a NABD queue
 *
 * @param name      Shared memory name (will be prefixed with /dev/shm/)
 * @param capacity  Number of slots in ring buffer (must be power of 2)
 * @param slot_size Maximum message size per slot (including header)
 * @param flags     NABD_CREATE | NABD_PRODUCER | NABD_CONSUMER
 *
 * @return Handle on success, NULL on failure (check errno)
 *
 * Example:
 *   // Producer creates the queue
 *   nabd_t* q = nabd_open("myqueue", 1024, 4096,
 *                         NABD_CREATE | NABD_PRODUCER);
 *
 *   // Consumer attaches to existing queue
 *   nabd_t* q = nabd_open("myqueue", 0, 0, NABD_CONSUMER);
 */
nabd_t *nabd_open(const char *name, size_t capacity, size_t slot_size,
                  int flags);

/**
 * Close a NABD queue
 *
 * @param q  Handle from nabd_open
 *
 * @return NABD_OK on success, error code on failure
 *
 * Note: This does NOT unlink the shared memory. Call nabd_unlink()
 *       to remove the shared memory segment.
 */
int nabd_close(nabd_t *q);

/**
 * Unlink (remove) shared memory segment
 *
 * @param name  Shared memory name
 *
 * @return NABD_OK on success, error code on failure
 *
 * Note: The segment is removed when the last process detaches.
 */
int nabd_unlink(const char *name);

/*
 * ============================================================================
 * Producer Functions
 * ============================================================================
 */

/**
 * Push a message to the queue (non-blocking)
 *
 * @param q     Handle from nabd_open
 * @param data  Pointer to message data
 * @param len   Message length in bytes
 *
 * @return NABD_OK on success
 *         NABD_FULL if buffer is full
 *         NABD_TOOBIG if message exceeds slot_size
 *
 * Zero-copy note: Data is copied once into shared memory.
 */
int nabd_push(nabd_t *q, const void *data, size_t len);

/**
 * Reserve a slot for zero-copy writing
 *
 * @param q     Handle from nabd_open
 * @param len   Required message length
 * @param slot  Output: pointer to write data directly
 *
 * @return NABD_OK on success
 *         NABD_FULL if buffer is full
 *         NABD_TOOBIG if message exceeds slot_size
 *
 * Must call nabd_commit() after writing to complete the push.
 */
int nabd_reserve(nabd_t *q, size_t len, void **slot);

/**
 * Commit a previously reserved slot
 *
 * @param q    Handle from nabd_open
 * @param len  Actual bytes written (may be <= reserved length)
 *
 * @return NABD_OK on success
 */
int nabd_commit(nabd_t *q, size_t len);

/*
 * ============================================================================
 * Consumer Functions
 * ============================================================================
 */

/**
 * Pop a message from the queue (non-blocking)
 *
 * @param q     Handle from nabd_open
 * @param buf   Buffer to copy message into
 * @param len   In: buffer capacity, Out: actual message length
 *
 * @return NABD_OK on success
 *         NABD_EMPTY if buffer is empty
 *         NABD_TOOBIG if message exceeds buffer capacity
 */
int nabd_pop(nabd_t *q, void *buf, size_t *len);

/**
 * Peek at next message without removing it
 *
 * @param q     Handle from nabd_open
 * @param data  Output: pointer to message data (read-only!)
 * @param len   Output: message length
 *
 * @return NABD_OK on success
 *         NABD_EMPTY if buffer is empty
 *
 * Warning: The returned pointer is only valid until nabd_release() is called.
 */
int nabd_peek(nabd_t *q, const void **data, size_t *len);

/**
 * Release a previously peeked message
 *
 * @param q  Handle from nabd_open
 *
 * @return NABD_OK on success
 */
int nabd_release(nabd_t *q);

/*
 * ============================================================================
 * Utility Functions
 * ============================================================================
 */

/**
 * Get queue statistics
 *
 * @param q      Handle from nabd_open
 * @param stats  Output: statistics structure
 *
 * @return NABD_OK on success
 */
int nabd_stats(nabd_t *q, nabd_stats_t *stats);

/**
 * Check if queue is empty
 *
 * @param q  Handle from nabd_open
 *
 * @return 1 if empty, 0 if not empty, negative on error
 */
int nabd_empty(nabd_t *q);

/**
 * Check if queue is full
 *
 * @param q  Handle from nabd_open
 *
 * @return 1 if full, 0 if not full, negative on error
 */
int nabd_full(nabd_t *q);

/**
 * Get error string for error code
 *
 * @param err  Error code
 *
 * @return Static string describing the error
 */
const char *nabd_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* NABD_H */
