/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Backpressure & Flow Control
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#ifndef NABD_BACKPRESSURE_H
#define NABD_BACKPRESSURE_H

#include "nabd.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Backpressure Callbacks
 * ============================================================================
 */

/**
 * Callback type for buffer fill level notifications
 *
 * @param q         Queue handle
 * @param fill_pct  Current fill percentage (0-100)
 * @param user_data User-provided context
 */
typedef void (*nabd_fill_callback_t)(nabd_t *q, int fill_pct, void *user_data);

/**
 * Backpressure configuration
 */
typedef struct {
  int high_watermark;           /* Trigger warning at this % (default: 80) */
  int low_watermark;            /* Resume normal at this % (default: 50) */
  nabd_fill_callback_t on_high; /* Called when reaching high watermark */
  nabd_fill_callback_t on_low;  /* Called when dropping to low watermark */
  void *user_data;              /* User context for callbacks */
} nabd_backpressure_config_t;

/*
 * ============================================================================
 * Backpressure API
 * ============================================================================
 */

/**
 * Configure backpressure callbacks
 *
 * @param q      Queue handle
 * @param config Backpressure configuration
 *
 * @return NABD_OK on success
 */
int nabd_set_backpressure(nabd_t *q, const nabd_backpressure_config_t *config);

/**
 * Push with blocking/retry on full buffer
 *
 * @param q          Queue handle
 * @param data       Message data
 * @param len        Message length
 * @param timeout_us Timeout in microseconds (0 = non-blocking, -1 = infinite)
 *
 * @return NABD_OK on success
 *         NABD_FULL if timeout expired
 *         NABD_TOOBIG if message too large
 */
int nabd_push_wait(nabd_t *q, const void *data, size_t len, int64_t timeout_us);

/**
 * Push with exponential backoff
 *
 * @param q             Queue handle
 * @param data          Message data
 * @param len           Message length
 * @param max_retries   Maximum retry attempts (0 = infinite)
 * @param base_delay_us Initial delay between retries in microseconds
 *
 * @return NABD_OK on success
 *         NABD_FULL if max retries exceeded
 */
int nabd_push_backoff(nabd_t *q, const void *data, size_t len, int max_retries,
                      int base_delay_us);

/**
 * Get current buffer fill percentage
 *
 * @param q Queue handle
 *
 * @return Fill percentage (0-100) or negative on error
 */
int nabd_fill_level(nabd_t *q);

/**
 * Check if buffer pressure is high
 *
 * @param q           Queue handle
 * @param threshold   Percentage threshold (e.g., 80 for 80%)
 *
 * @return 1 if fill >= threshold, 0 otherwise, negative on error
 */
int nabd_is_pressured(nabd_t *q, int threshold);

#ifdef __cplusplus
}
#endif

#endif /* NABD_BACKPRESSURE_H */
