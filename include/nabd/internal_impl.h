/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Internal Implementation Header
 * (Shared between source files only - NOT public API)
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#ifndef NABD_INTERNAL_IMPL_H
#define NABD_INTERNAL_IMPL_H

#include "internal.h"
#include "types.h"

/*
 * Internal NABD handle structure
 */
struct nabd {
  char *name;           /* Shared memory name */
  int fd;               /* File descriptor */
  int flags;            /* Open flags */
  size_t size;          /* Total mapped size */
  nabd_control_t *ctrl; /* Control block pointer */
  uint8_t *buffer;      /* Ring buffer pointer */

  /* Cached values for fast access */
  size_t capacity;  /* Number of slots */
  size_t slot_size; /* Bytes per slot */
  size_t mask;      /* capacity - 1 for fast modulo */

  /* Zero-copy state */
  int reserved;         /* Whether a slot is reserved */
  uint64_t reserve_pos; /* Reserved slot position */

  /* Multi-consumer extension (NULL if not used) */
  nabd_multi_consumer_t *multi; /* Multi-consumer control block */
};

/*
 * Internal consumer handle structure
 */
struct nabd_consumer {
  nabd_t *queue;                /* Parent queue */
  nabd_consumer_group_t *group; /* Consumer group in shared memory */
  uint32_t group_id;            /* Group identifier */
};

/*
 * Helper: Get slot pointer by index (hot path - force inline)
 */
NABD_INLINE void *nabd_get_slot(struct nabd *q, uint64_t index) {
  return q->buffer + nabd_mod_pow2(index, q->mask) * q->slot_size;
}

/*
 * Helper: Get slot header
 */
NABD_INLINE nabd_slot_header_t *nabd_get_slot_header(struct nabd *q,
                                                     uint64_t index) {
  return (nabd_slot_header_t *)nabd_get_slot(q, index);
}

/*
 * Helper: Get slot payload
 */
NABD_INLINE void *nabd_get_slot_payload(struct nabd *q, uint64_t index) {
  return (uint8_t *)nabd_get_slot(q, index) + sizeof(nabd_slot_header_t);
}

#endif /* NABD_INTERNAL_IMPL_H */
