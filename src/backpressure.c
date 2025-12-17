/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Backpressure & Flow Control Implementation
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#include "../include/nabd/backpressure.h"
#include "../include/nabd/internal.h"
#include "../include/nabd/types.h"

#include <errno.h>
#include <time.h>

/*
 * Get current time in microseconds
 */
static inline int64_t get_time_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/*
 * Sleep for microseconds
 */
static inline void sleep_us(int64_t us) {
  if (us <= 0)
    return;
  struct timespec ts;
  ts.tv_sec = us / 1000000;
  ts.tv_nsec = (us % 1000000) * 1000;
  nanosleep(&ts, NULL);
}

/*
 * Internal: Access queue internals
 * Note: This requires including the internal nabd struct definition
 */
extern size_t nabd_get_capacity(nabd_t *q);
extern uint64_t nabd_get_head(nabd_t *q);
extern uint64_t nabd_get_tail(nabd_t *q);

/*
 * Get current buffer fill percentage
 */
int nabd_fill_level(nabd_t *q) {
  if (NABD_UNLIKELY(!q))
    return NABD_INVALID;

  nabd_stats_t stats;
  if (nabd_stats(q, &stats) != NABD_OK) {
    return NABD_INVALID;
  }

  if (stats.capacity == 0)
    return 0;

  return (int)((stats.used * 100) / stats.capacity);
}

/*
 * Check if buffer pressure is high
 */
int nabd_is_pressured(nabd_t *q, int threshold) {
  int level = nabd_fill_level(q);
  if (level < 0)
    return level;
  return level >= threshold ? 1 : 0;
}

/*
 * Push with timeout
 */
int nabd_push_wait(nabd_t *q, const void *data, size_t len,
                   int64_t timeout_us) {
  if (NABD_UNLIKELY(!q || !data))
    return NABD_INVALID;

  /* Try immediate push first */
  int ret = nabd_push(q, data, len);
  if (ret != NABD_FULL) {
    return ret;
  }

  /* Non-blocking mode */
  if (timeout_us == 0) {
    return NABD_FULL;
  }

  int64_t deadline = (timeout_us > 0) ? get_time_us() + timeout_us : 0;
  int spin_count = 0;
  const int MAX_SPINS = 100;
  const int64_t SLEEP_US = 10; /* 10 microseconds initial sleep */

  while (1) {
    /* Try push again */
    ret = nabd_push(q, data, len);
    if (ret != NABD_FULL) {
      return ret;
    }

    /* Check timeout */
    if (timeout_us > 0 && get_time_us() >= deadline) {
      return NABD_FULL;
    }

    /* Spin for a bit before sleeping */
    if (spin_count < MAX_SPINS) {
      NABD_CPU_PAUSE();
      spin_count++;
    } else {
      /* Sleep with exponential backoff, capped at 1ms */
      int64_t sleep_time = SLEEP_US * (spin_count / MAX_SPINS);
      if (sleep_time > 1000)
        sleep_time = 1000;
      sleep_us(sleep_time);
    }
  }
}

/*
 * Push with exponential backoff
 */
int nabd_push_backoff(nabd_t *q, const void *data, size_t len, int max_retries,
                      int base_delay_us) {
  if (NABD_UNLIKELY(!q || !data))
    return NABD_INVALID;

  int delay = base_delay_us > 0 ? base_delay_us : 1;
  int retries = 0;
  const int MAX_DELAY = 100000; /* Cap at 100ms */

  while (1) {
    int ret = nabd_push(q, data, len);
    if (ret != NABD_FULL) {
      return ret;
    }

    retries++;
    if (max_retries > 0 && retries >= max_retries) {
      return NABD_FULL;
    }

    /* Sleep with exponential backoff */
    sleep_us(delay);

    /* Double delay for next iteration */
    delay *= 2;
    if (delay > MAX_DELAY) {
      delay = MAX_DELAY;
    }
  }
}

/*
 * Set backpressure configuration
 * Note: This is a simplified implementation. Full implementation would
 *       require storing callbacks in the queue structure and checking
 *       watermarks on each push.
 */
int nabd_set_backpressure(nabd_t *q, const nabd_backpressure_config_t *config) {
  if (!q || !config)
    return NABD_INVALID;

  /* For now, just validate the configuration */
  if (config->high_watermark < 0 || config->high_watermark > 100) {
    return NABD_INVALID;
  }
  if (config->low_watermark < 0 || config->low_watermark > 100) {
    return NABD_INVALID;
  }
  if (config->low_watermark >= config->high_watermark) {
    return NABD_INVALID;
  }

  /* TODO: Store in queue and integrate with push operations */

  return NABD_OK;
}
