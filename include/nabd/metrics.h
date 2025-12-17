/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Observability & Metrics
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#ifndef NABD_METRICS_H
#define NABD_METRICS_H

#include "nabd.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Metrics Structures
 * ============================================================================
 */

/**
 * Detailed queue metrics
 */
typedef struct {
  /* Position metrics */
  uint64_t head;    /* Current head position */
  uint64_t tail;    /* Current tail (min across groups) */
  uint64_t pending; /* Messages pending (head - tail) */

  /* Capacity metrics */
  uint64_t capacity;   /* Total slots */
  uint64_t slot_size;  /* Bytes per slot */
  uint64_t used_bytes; /* Approximate bytes in use */
  int fill_pct;        /* Fill percentage (0-100) */

  /* Throughput metrics (if tracking enabled) */
  uint64_t total_pushed; /* Total messages pushed */
  uint64_t total_popped; /* Total messages popped */
  uint64_t push_per_sec; /* Recent push rate */
  uint64_t pop_per_sec;  /* Recent pop rate */

  /* Latency metrics (if tracking enabled) */
  uint64_t avg_latency_ns; /* Average message latency */
  uint64_t p50_latency_ns; /* 50th percentile latency */
  uint64_t p99_latency_ns; /* 99th percentile latency */
  uint64_t max_latency_ns; /* Maximum observed latency */

  /* Health metrics */
  uint64_t full_events;  /* Times buffer was full */
  uint64_t empty_events; /* Times buffer was empty */
} nabd_metrics_t;

/**
 * Metric snapshot for time-series analysis
 */
typedef struct {
  uint64_t timestamp_ns; /* When snapshot was taken */
  uint64_t head;         /* Head at snapshot */
  uint64_t tail;         /* Tail at snapshot */
  uint64_t pushed;       /* Messages pushed since last snapshot */
  uint64_t popped;       /* Messages popped since last snapshot */
} nabd_snapshot_t;

/*
 * ============================================================================
 * Metrics API
 * ============================================================================
 */

/**
 * Get detailed queue metrics
 *
 * @param q        Queue handle
 * @param metrics  Output: metrics structure
 *
 * @return NABD_OK on success
 */
int nabd_get_metrics(nabd_t *q, nabd_metrics_t *metrics);

/**
 * Take a metrics snapshot (lightweight)
 *
 * @param q        Queue handle
 * @param snapshot Output: snapshot structure
 *
 * @return NABD_OK on success
 */
int nabd_take_snapshot(nabd_t *q, nabd_snapshot_t *snapshot);

/**
 * Get current throughput (messages per second)
 *
 * Based on comparing two snapshots
 *
 * @param prev     Previous snapshot
 * @param curr     Current snapshot
 *
 * @return Messages per second, or 0 on error
 */
uint64_t nabd_calc_throughput(const nabd_snapshot_t *prev,
                              const nabd_snapshot_t *curr);

/**
 * Format metrics as string for logging
 *
 * @param metrics  Metrics to format
 * @param buf      Output buffer
 * @param bufsize  Buffer size
 *
 * @return Number of bytes written, or -1 on error
 */
int nabd_format_metrics(const nabd_metrics_t *metrics, char *buf,
                        size_t bufsize);

/**
 * Format metrics as JSON
 *
 * @param metrics  Metrics to format
 * @param buf      Output buffer
 * @param bufsize  Buffer size
 *
 * @return Number of bytes written, or -1 on error
 */
int nabd_format_metrics_json(const nabd_metrics_t *metrics, char *buf,
                             size_t bufsize);

#ifdef __cplusplus
}
#endif

#endif /* NABD_METRICS_H */
