/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Core Implementation (Optimized)
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#include "../include/nabd/nabd.h"
#include "../include/nabd/internal.h"
#include "../include/nabd/types.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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
NABD_INLINE void *get_slot(nabd_t *q, uint64_t index) {
  return q->buffer + nabd_mod_pow2(index, q->mask) * q->slot_size;
}

/*
 * Helper: Get slot header
 */
NABD_INLINE nabd_slot_header_t *get_slot_header(nabd_t *q, uint64_t index) {
  return (nabd_slot_header_t *)get_slot(q, index);
}

/*
 * Helper: Get slot payload
 */
NABD_INLINE void *get_slot_payload(nabd_t *q, uint64_t index) {
  return (uint8_t *)get_slot(q, index) + sizeof(nabd_slot_header_t);
}

/*
 * Open or create a NABD queue
 */
nabd_t *nabd_open(const char *name, size_t capacity, size_t slot_size,
                  int flags) {
  if (!name) {
    errno = EINVAL;
    return NULL;
  }

  /* Validate flags */
  int is_create = flags & NABD_CREATE;
  int is_producer = flags & NABD_PRODUCER;
  int is_consumer = flags & NABD_CONSUMER;

  if (!is_producer && !is_consumer) {
    errno = EINVAL;
    return NULL;
  }

  /* For create, validate capacity and slot_size */
  if (is_create) {
    if (capacity == 0)
      capacity = NABD_DEFAULT_CAPACITY;
    if (slot_size == 0)
      slot_size = NABD_DEFAULT_SLOT_SIZE;

    /* Ensure power of 2 for capacity */
    if (NABD_UNLIKELY(!nabd_is_power_of_2(capacity))) {
      capacity = nabd_next_power_of_2(capacity);
    }

    /* Minimum slot size */
    if (slot_size < sizeof(nabd_slot_header_t) + 8) {
      slot_size = sizeof(nabd_slot_header_t) + 8;
    }
  }

  /* Allocate handle */
  nabd_t *q = calloc(1, sizeof(nabd_t));
  if (!q) {
    return NULL;
  }

  q->name = strdup(name);
  if (!q->name) {
    free(q);
    return NULL;
  }

  q->flags = flags;
  q->fd = -1;

  /* Open or create shared memory */
  int shm_flags = O_RDWR;
  if (is_create) {
    shm_flags |= O_CREAT | O_EXCL;
  }

  q->fd = shm_open(name, shm_flags, 0666);
  if (q->fd < 0) {
    /* If create failed with EEXIST, try opening existing */
    if (is_create && errno == EEXIST) {
      q->fd = shm_open(name, O_RDWR, 0666);
    }
    if (q->fd < 0) {
      free(q->name);
      free(q);
      return NULL;
    }
  }

  size_t total_size;

  if (is_create) {
    /* Set size and initialize */
    total_size = sizeof(nabd_control_t) + (capacity * slot_size);

    if (ftruncate(q->fd, total_size) < 0) {
      close(q->fd);
      shm_unlink(name);
      free(q->name);
      free(q);
      return NULL;
    }

    /* Map shared memory */
    void *ptr =
        mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, q->fd, 0);
    if (ptr == MAP_FAILED) {
      close(q->fd);
      shm_unlink(name);
      free(q->name);
      free(q);
      return NULL;
    }

    q->ctrl = (nabd_control_t *)ptr;
    q->buffer = (uint8_t *)ptr + sizeof(nabd_control_t);
    q->size = total_size;

    /* Initialize control block */
    memset(q->ctrl, 0, sizeof(nabd_control_t));
    q->ctrl->magic = NABD_MAGIC;
    q->ctrl->version = (NABD_VERSION_MAJOR << 16) | NABD_VERSION_MINOR;
    q->ctrl->capacity = capacity;
    q->ctrl->slot_size = slot_size;
    q->ctrl->buffer_offset = sizeof(nabd_control_t);
    atomic_store(&q->ctrl->head, 0);
    atomic_store(&q->ctrl->tail, 0);

  } else {
    /* Map just enough to read control block first */
    void *ptr = mmap(NULL, sizeof(nabd_control_t), PROT_READ | PROT_WRITE,
                     MAP_SHARED, q->fd, 0);
    if (ptr == MAP_FAILED) {
      close(q->fd);
      free(q->name);
      free(q);
      return NULL;
    }

    nabd_control_t *ctrl_tmp = (nabd_control_t *)ptr;

    /* Validate magic */
    if (ctrl_tmp->magic != NABD_MAGIC) {
      munmap(ptr, sizeof(nabd_control_t));
      close(q->fd);
      free(q->name);
      free(q);
      errno = EINVAL;
      return NULL;
    }

    capacity = ctrl_tmp->capacity;
    slot_size = ctrl_tmp->slot_size;
    total_size = sizeof(nabd_control_t) + (capacity * slot_size);

    /* Unmap and remap full size */
    munmap(ptr, sizeof(nabd_control_t));

    ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, q->fd, 0);
    if (ptr == MAP_FAILED) {
      close(q->fd);
      free(q->name);
      free(q);
      return NULL;
    }

    q->ctrl = (nabd_control_t *)ptr;
    q->buffer = (uint8_t *)ptr + sizeof(nabd_control_t);
    q->size = total_size;
  }

  /* Cache values */
  q->capacity = capacity;
  q->slot_size = slot_size;
  q->mask = capacity - 1;
  q->reserved = 0;

  return q;
}

/*
 * Close a NABD queue
 */
int nabd_close(nabd_t *q) {
  if (!q)
    return NABD_INVALID;

  if (q->ctrl) {
    munmap(q->ctrl, q->size);
  }

  if (q->fd >= 0) {
    close(q->fd);
  }

  free(q->name);
  free(q);

  return NABD_OK;
}

/*
 * Unlink shared memory
 */
int nabd_unlink(const char *name) {
  if (!name)
    return NABD_INVALID;

  if (shm_unlink(name) < 0) {
    return NABD_SYSERR;
  }

  return NABD_OK;
}

/*
 * Push a message (non-blocking) - HOT PATH
 */
int nabd_push(nabd_t *q, const void *data, size_t len) {
  if (NABD_UNLIKELY(!q || !data))
    return NABD_INVALID;

  size_t max_payload = q->slot_size - sizeof(nabd_slot_header_t);
  if (NABD_UNLIKELY(len > max_payload))
    return NABD_TOOBIG;

  /* Load head (our position) - relaxed ok, it's our variable */
  uint64_t head = NABD_LOAD_RELAXED(&q->ctrl->head);

  /* Load tail (consumer position) with acquire to sync */
  uint64_t tail = NABD_LOAD_ACQUIRE(&q->ctrl->tail);

  /* Check if full */
  if (NABD_UNLIKELY(head - tail >= q->capacity)) {
    return NABD_FULL;
  }

  /* Get slot and prefetch for writing */
  void *slot = get_slot(q, head);
  NABD_PREFETCH_WRITE(slot);

  nabd_slot_header_t *hdr = (nabd_slot_header_t *)slot;
  void *payload = (uint8_t *)slot + sizeof(nabd_slot_header_t);

  /* Copy data */
  memcpy(payload, data, len);

  /* Fill header */
  hdr->length = (uint16_t)len;
  hdr->flags = 0;
  hdr->sequence = (uint32_t)head;

  /* Publish: release store to head */
  NABD_STORE_RELEASE(&q->ctrl->head, head + 1);

  return NABD_OK;
}

/*
 * Pop a message (non-blocking) - HOT PATH
 */
int nabd_pop(nabd_t *q, void *buf, size_t *len) {
  if (NABD_UNLIKELY(!q || !buf || !len))
    return NABD_INVALID;

  /* Load tail (our position) - relaxed ok, it's our variable */
  uint64_t tail = NABD_LOAD_RELAXED(&q->ctrl->tail);

  /* Load head (producer position) with acquire to see data */
  uint64_t head = NABD_LOAD_ACQUIRE(&q->ctrl->head);

  /* Check if empty */
  if (NABD_UNLIKELY(tail == head)) {
    return NABD_EMPTY;
  }

  /* Get slot and prefetch for reading */
  void *slot = get_slot(q, tail);
  NABD_PREFETCH_READ(slot);

  /* Also prefetch next slot for speculative read */
  if (NABD_LIKELY(tail + 1 < head)) {
    NABD_PREFETCH_READ(get_slot(q, tail + 1));
  }

  nabd_slot_header_t *hdr = (nabd_slot_header_t *)slot;
  void *payload = (uint8_t *)slot + sizeof(nabd_slot_header_t);

  size_t msg_len = hdr->length;

  /* Check buffer size */
  if (NABD_UNLIKELY(msg_len > *len)) {
    *len = msg_len;
    return NABD_TOOBIG;
  }

  memcpy(buf, payload, msg_len);
  *len = msg_len;

  /* Signal consumption: release store to tail */
  NABD_STORE_RELEASE(&q->ctrl->tail, tail + 1);

  return NABD_OK;
}

/*
 * Reserve a slot for zero-copy write
 */
int nabd_reserve(nabd_t *q, size_t len, void **slot) {
  if (!q || !slot)
    return NABD_INVALID;
  if (q->reserved)
    return NABD_INVALID;

  size_t max_payload = q->slot_size - sizeof(nabd_slot_header_t);
  if (len > max_payload)
    return NABD_TOOBIG;

  uint64_t head = atomic_load_explicit(&q->ctrl->head, memory_order_relaxed);
  uint64_t tail = atomic_load_explicit(&q->ctrl->tail, memory_order_acquire);

  if (head - tail >= q->capacity) {
    return NABD_FULL;
  }

  q->reserved = 1;
  q->reserve_pos = head;
  *slot = get_slot_payload(q, head);

  return NABD_OK;
}

/*
 * Commit a reserved slot
 */
int nabd_commit(nabd_t *q, size_t len) {
  if (!q || !q->reserved)
    return NABD_INVALID;

  nabd_slot_header_t *hdr = get_slot_header(q, q->reserve_pos);
  hdr->length = (uint16_t)len;
  hdr->flags = 0;
  hdr->sequence = (uint32_t)q->reserve_pos;

  atomic_store_explicit(&q->ctrl->head, q->reserve_pos + 1,
                        memory_order_release);

  q->reserved = 0;

  return NABD_OK;
}

/*
 * Peek at next message
 */
int nabd_peek(nabd_t *q, const void **data, size_t *len) {
  if (!q || !data || !len)
    return NABD_INVALID;

  uint64_t tail = atomic_load_explicit(&q->ctrl->tail, memory_order_relaxed);
  uint64_t head = atomic_load_explicit(&q->ctrl->head, memory_order_acquire);

  if (tail == head) {
    return NABD_EMPTY;
  }

  nabd_slot_header_t *hdr = get_slot_header(q, tail);
  *data = get_slot_payload(q, tail);
  *len = hdr->length;

  return NABD_OK;
}

/*
 * Release a peeked message
 */
int nabd_release(nabd_t *q) {
  if (!q)
    return NABD_INVALID;

  uint64_t tail = atomic_load_explicit(&q->ctrl->tail, memory_order_relaxed);
  atomic_store_explicit(&q->ctrl->tail, tail + 1, memory_order_release);

  return NABD_OK;
}

/*
 * Get queue statistics
 */
int nabd_stats(nabd_t *q, nabd_stats_t *stats) {
  if (!q || !stats)
    return NABD_INVALID;

  stats->head = atomic_load_explicit(&q->ctrl->head, memory_order_relaxed);
  stats->tail = atomic_load_explicit(&q->ctrl->tail, memory_order_relaxed);
  stats->capacity = q->capacity;
  stats->slot_size = q->slot_size;
  stats->used = stats->head - stats->tail;

  return NABD_OK;
}

/*
 * Check if empty
 */
int nabd_empty(nabd_t *q) {
  if (!q)
    return NABD_INVALID;

  uint64_t tail = atomic_load_explicit(&q->ctrl->tail, memory_order_relaxed);
  uint64_t head = atomic_load_explicit(&q->ctrl->head, memory_order_acquire);

  return tail == head ? 1 : 0;
}

/*
 * Check if full
 */
int nabd_full(nabd_t *q) {
  if (!q)
    return NABD_INVALID;

  uint64_t head = atomic_load_explicit(&q->ctrl->head, memory_order_relaxed);
  uint64_t tail = atomic_load_explicit(&q->ctrl->tail, memory_order_acquire);

  return (head - tail >= q->capacity) ? 1 : 0;
}

/*
 * Get error string
 */
const char *nabd_strerror(int err) {
  switch (err) {
  case NABD_OK:
    return "Success";
  case NABD_EMPTY:
    return "Buffer empty";
  case NABD_FULL:
    return "Buffer full";
  case NABD_NOMEM:
    return "Out of memory";
  case NABD_INVALID:
    return "Invalid argument";
  case NABD_EXISTS:
    return "Already exists";
  case NABD_NOTFOUND:
    return "Not found";
  case NABD_TOOBIG:
    return "Message too large";
  case NABD_CORRUPTED:
    return "Data corrupted";
  case NABD_VERSION:
    return "Version mismatch";
  case NABD_PERMISSION:
    return "Permission denied";
  case NABD_SYSERR:
    return "System error";
  default:
    return "Unknown error";
  }
}

/*
 * ============================================================================
 * Multi-Consumer Support Implementation
 * ============================================================================
 */

#define NABD_MULTI_MAGIC 0x4D4C544E55425444ULL /* "NABDMULTI" */

/*
 * Create a consumer group
 */
nabd_consumer_t *nabd_consumer_create(nabd_t *q, uint32_t group_id) {
  if (NABD_UNLIKELY(!q))
    return NULL;

  /* Find an available group slot */
  nabd_multi_consumer_t *multi = q->multi;
  if (!multi) {
    /* No multi-consumer support initialized */
    errno = EINVAL;
    return NULL;
  }

  nabd_consumer_group_t *group = NULL;
  uint32_t assigned_id = group_id;

  for (int i = 0; i < NABD_MAX_CONSUMERS; i++) {
    uint32_t expected = 0;
    if (NABD_CAS_ACQ_REL(&multi->groups[i].active, &expected, 1)) {
      /* Successfully claimed this slot */
      group = &multi->groups[i];
      assigned_id = (group_id != 0) ? group_id : (uint32_t)(i + 1);
      group->group_id = assigned_id;

      /* Initialize tail to current head (start from now) */
      uint64_t head = NABD_LOAD_ACQUIRE(&q->ctrl->head);
      NABD_STORE_RELEASE(&group->tail, head);
      break;
    }
  }

  if (!group) {
    errno = ENOMEM; /* No slots available */
    return NULL;
  }

  /* Allocate consumer handle */
  nabd_consumer_t *c = calloc(1, sizeof(nabd_consumer_t));
  if (!c) {
    NABD_STORE_RELEASE(&group->active, 0); /* Release slot */
    return NULL;
  }

  c->queue = q;
  c->group = group;
  c->group_id = assigned_id;

  return c;
}

/*
 * Join an existing consumer group
 */
nabd_consumer_t *nabd_consumer_join(nabd_t *q, uint32_t group_id) {
  if (NABD_UNLIKELY(!q || group_id == 0))
    return NULL;

  nabd_multi_consumer_t *multi = q->multi;
  if (!multi) {
    errno = EINVAL;
    return NULL;
  }

  /* Find the group by ID */
  nabd_consumer_group_t *group = NULL;
  for (int i = 0; i < NABD_MAX_CONSUMERS; i++) {
    if (NABD_LOAD_ACQUIRE(&multi->groups[i].active) &&
        multi->groups[i].group_id == group_id) {
      group = &multi->groups[i];
      break;
    }
  }

  if (!group) {
    errno = ENOENT;
    return NULL;
  }

  /* Allocate consumer handle */
  nabd_consumer_t *c = calloc(1, sizeof(nabd_consumer_t));
  if (!c)
    return NULL;

  c->queue = q;
  c->group = group;
  c->group_id = group_id;

  return c;
}

/*
 * Close a consumer handle
 */
int nabd_consumer_close(nabd_consumer_t *c) {
  if (!c)
    return NABD_INVALID;

  /* Don't deactivate the group - other consumers may be using it */
  free(c);
  return NABD_OK;
}

/*
 * Pop a message for this consumer group
 */
int nabd_consumer_pop(nabd_consumer_t *c, void *buf, size_t *len) {
  if (NABD_UNLIKELY(!c || !buf || !len))
    return NABD_INVALID;

  nabd_t *q = c->queue;
  nabd_consumer_group_t *group = c->group;

  /* Load our tail position */
  uint64_t tail = NABD_LOAD_RELAXED(&group->tail);

  /* Load head with acquire to see producer's data */
  uint64_t head = NABD_LOAD_ACQUIRE(&q->ctrl->head);

  /* Check if empty for this group */
  if (NABD_UNLIKELY(tail >= head)) {
    return NABD_EMPTY;
  }

  /* Get slot and prefetch */
  void *slot = get_slot(q, tail);
  NABD_PREFETCH_READ(slot);

  nabd_slot_header_t *hdr = (nabd_slot_header_t *)slot;
  void *payload = (uint8_t *)slot + sizeof(nabd_slot_header_t);

  size_t msg_len = hdr->length;

  if (NABD_UNLIKELY(msg_len > *len)) {
    *len = msg_len;
    return NABD_TOOBIG;
  }

  memcpy(buf, payload, msg_len);
  *len = msg_len;

  /* Advance this group's tail */
  NABD_STORE_RELEASE(&group->tail, tail + 1);

  return NABD_OK;
}

/*
 * Peek at next message for this consumer group
 */
int nabd_consumer_peek(nabd_consumer_t *c, const void **data, size_t *len) {
  if (NABD_UNLIKELY(!c || !data || !len))
    return NABD_INVALID;

  nabd_t *q = c->queue;
  nabd_consumer_group_t *group = c->group;

  uint64_t tail = NABD_LOAD_RELAXED(&group->tail);
  uint64_t head = NABD_LOAD_ACQUIRE(&q->ctrl->head);

  if (NABD_UNLIKELY(tail >= head)) {
    return NABD_EMPTY;
  }

  nabd_slot_header_t *hdr = get_slot_header(q, tail);
  *data = get_slot_payload(q, tail);
  *len = hdr->length;

  return NABD_OK;
}

/*
 * Release a peeked message for this consumer group
 */
int nabd_consumer_release(nabd_consumer_t *c) {
  if (!c)
    return NABD_INVALID;

  uint64_t tail = NABD_LOAD_RELAXED(&c->group->tail);
  NABD_STORE_RELEASE(&c->group->tail, tail + 1);

  return NABD_OK;
}

/*
 * Get consumer group statistics
 */
int nabd_consumer_stats(nabd_consumer_t *c, nabd_consumer_stats_t *stats) {
  if (!c || !stats)
    return NABD_INVALID;

  nabd_t *q = c->queue;
  uint64_t head = NABD_LOAD_RELAXED(&q->ctrl->head);
  uint64_t tail = NABD_LOAD_RELAXED(&c->group->tail);

  stats->group_id = c->group_id;
  stats->active = NABD_LOAD_RELAXED(&c->group->active);
  stats->tail = tail;
  stats->lag = (head > tail) ? (head - tail) : 0;

  return NABD_OK;
}

/*
 * Get minimum tail across all consumer groups
 * This determines how far back the buffer must retain data
 */
uint64_t nabd_min_tail(nabd_t *q) {
  if (!q || !q->multi) {
    return NABD_LOAD_RELAXED(&q->ctrl->tail);
  }

  uint64_t min_tail = UINT64_MAX;
  nabd_multi_consumer_t *multi = q->multi;

  for (int i = 0; i < NABD_MAX_CONSUMERS; i++) {
    if (NABD_LOAD_RELAXED(&multi->groups[i].active)) {
      uint64_t tail = NABD_LOAD_RELAXED(&multi->groups[i].tail);
      if (tail < min_tail) {
        min_tail = tail;
      }
    }
  }

  /* If no consumer groups active, fall back to single consumer tail */
  if (min_tail == UINT64_MAX) {
    min_tail = NABD_LOAD_RELAXED(&q->ctrl->tail);
  }

  return min_tail;
}
