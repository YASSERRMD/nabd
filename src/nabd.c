/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Core Implementation
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#define _GNU_SOURCE
#include "../include/nabd/nabd.h"
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
};

/*
 * Helper: Check if n is a power of 2
 */
static inline int is_power_of_2(size_t n) { return n && !(n & (n - 1)); }

/*
 * Helper: Round up to next power of 2
 */
static inline size_t next_power_of_2(size_t n) {
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  n++;
  return n;
}

/*
 * Helper: Get slot pointer by index
 */
static inline void *get_slot(nabd_t *q, uint64_t index) {
  return q->buffer + (index & q->mask) * q->slot_size;
}

/*
 * Helper: Get slot header
 */
static inline nabd_slot_header_t *get_slot_header(nabd_t *q, uint64_t index) {
  return (nabd_slot_header_t *)get_slot(q, index);
}

/*
 * Helper: Get slot payload
 */
static inline void *get_slot_payload(nabd_t *q, uint64_t index) {
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
    if (!is_power_of_2(capacity)) {
      capacity = next_power_of_2(capacity);
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
 * Push a message (non-blocking)
 */
int nabd_push(nabd_t *q, const void *data, size_t len) {
  if (!q || !data)
    return NABD_INVALID;

  size_t max_payload = q->slot_size - sizeof(nabd_slot_header_t);
  if (len > max_payload)
    return NABD_TOOBIG;

  /* Load head (our position) */
  uint64_t head = atomic_load_explicit(&q->ctrl->head, memory_order_relaxed);

  /* Load tail (consumer position) with acquire to sync */
  uint64_t tail = atomic_load_explicit(&q->ctrl->tail, memory_order_acquire);

  /* Check if full */
  if (head - tail >= q->capacity) {
    return NABD_FULL;
  }

  /* Get slot and write data */
  nabd_slot_header_t *hdr = get_slot_header(q, head);
  void *payload = get_slot_payload(q, head);

  memcpy(payload, data, len);
  hdr->length = (uint16_t)len;
  hdr->flags = 0;
  hdr->sequence = (uint32_t)head;

  /* Publish: release store to head */
  atomic_store_explicit(&q->ctrl->head, head + 1, memory_order_release);

  return NABD_OK;
}

/*
 * Pop a message (non-blocking)
 */
int nabd_pop(nabd_t *q, void *buf, size_t *len) {
  if (!q || !buf || !len)
    return NABD_INVALID;

  /* Load tail (our position) */
  uint64_t tail = atomic_load_explicit(&q->ctrl->tail, memory_order_relaxed);

  /* Load head (producer position) with acquire to see data */
  uint64_t head = atomic_load_explicit(&q->ctrl->head, memory_order_acquire);

  /* Check if empty */
  if (tail == head) {
    return NABD_EMPTY;
  }

  /* Get slot and read data */
  nabd_slot_header_t *hdr = get_slot_header(q, tail);
  void *payload = get_slot_payload(q, tail);

  size_t msg_len = hdr->length;

  /* Check buffer size */
  if (msg_len > *len) {
    *len = msg_len;
    return NABD_TOOBIG;
  }

  memcpy(buf, payload, msg_len);
  *len = msg_len;

  /* Signal consumption: release store to tail */
  atomic_store_explicit(&q->ctrl->tail, tail + 1, memory_order_release);

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
