/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Observability & Metrics Implementation
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#include "../include/nabd/metrics.h"
#include "../include/nabd/internal_impl.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Get current time in nanoseconds
 */
static uint64_t get_nanos(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/*
 * Get detailed queue metrics
 */
int nabd_get_metrics(nabd_t *q, nabd_metrics_t *metrics) {
  if (!q || !metrics)
    return NABD_INVALID;

  memset(metrics, 0, sizeof(*metrics));

  /* Position metrics */
  metrics->head = NABD_LOAD_RELAXED(&q->ctrl->head);
  metrics->tail = NABD_LOAD_RELAXED(&q->ctrl->tail);
  metrics->pending =
      (metrics->head >= metrics->tail) ? (metrics->head - metrics->tail) : 0;

  /* Capacity metrics */
  metrics->capacity = q->capacity;
  metrics->slot_size = q->slot_size;
  metrics->used_bytes = metrics->pending * q->slot_size;

  if (q->capacity > 0) {
    metrics->fill_pct = (int)((metrics->pending * 100) / q->capacity);
  }

  /* Throughput/latency metrics are placeholders */
  /* Full implementation would require tracking in push/pop */
  metrics->total_pushed = metrics->head;
  metrics->total_popped = metrics->tail;

  return NABD_OK;
}

/*
 * Take a lightweight snapshot
 */
int nabd_take_snapshot(nabd_t *q, nabd_snapshot_t *snapshot) {
  if (!q || !snapshot)
    return NABD_INVALID;

  snapshot->timestamp_ns = get_nanos();
  snapshot->head = NABD_LOAD_RELAXED(&q->ctrl->head);
  snapshot->tail = NABD_LOAD_RELAXED(&q->ctrl->tail);
  snapshot->pushed = snapshot->head; /* Cumulative */
  snapshot->popped = snapshot->tail; /* Cumulative */

  return NABD_OK;
}

/*
 * Calculate throughput between two snapshots
 */
uint64_t nabd_calc_throughput(const nabd_snapshot_t *prev,
                              const nabd_snapshot_t *curr) {
  if (!prev || !curr)
    return 0;

  uint64_t time_diff = curr->timestamp_ns - prev->timestamp_ns;
  if (time_diff == 0)
    return 0;

  uint64_t msg_diff =
      (curr->pushed - prev->pushed) + (curr->popped - prev->popped);

  /* Messages per second */
  return (msg_diff * 1000000000ULL) / time_diff;
}

/*
 * Format metrics as string
 */
int nabd_format_metrics(const nabd_metrics_t *metrics, char *buf,
                        size_t bufsize) {
  if (!metrics || !buf || bufsize == 0)
    return -1;

  return snprintf(buf, bufsize,
                  "NABD Queue Metrics:\n"
                  "  Head: %llu, Tail: %llu, Pending: %llu\n"
                  "  Capacity: %llu slots (%llu bytes/slot)\n"
                  "  Fill: %d%% (%llu bytes used)\n"
                  "  Total pushed: %llu, popped: %llu\n",
                  (unsigned long long)metrics->head,
                  (unsigned long long)metrics->tail,
                  (unsigned long long)metrics->pending,
                  (unsigned long long)metrics->capacity,
                  (unsigned long long)metrics->slot_size, metrics->fill_pct,
                  (unsigned long long)metrics->used_bytes,
                  (unsigned long long)metrics->total_pushed,
                  (unsigned long long)metrics->total_popped);
}

/*
 * Format metrics as JSON
 */
int nabd_format_metrics_json(const nabd_metrics_t *metrics, char *buf,
                             size_t bufsize) {
  if (!metrics || !buf || bufsize == 0)
    return -1;

  return snprintf(buf, bufsize,
                  "{\n"
                  "  \"head\": %llu,\n"
                  "  \"tail\": %llu,\n"
                  "  \"pending\": %llu,\n"
                  "  \"capacity\": %llu,\n"
                  "  \"slot_size\": %llu,\n"
                  "  \"fill_pct\": %d,\n"
                  "  \"used_bytes\": %llu,\n"
                  "  \"total_pushed\": %llu,\n"
                  "  \"total_popped\": %llu\n"
                  "}",
                  (unsigned long long)metrics->head,
                  (unsigned long long)metrics->tail,
                  (unsigned long long)metrics->pending,
                  (unsigned long long)metrics->capacity,
                  (unsigned long long)metrics->slot_size, metrics->fill_pct,
                  (unsigned long long)metrics->used_bytes,
                  (unsigned long long)metrics->total_pushed,
                  (unsigned long long)metrics->total_popped);
}
