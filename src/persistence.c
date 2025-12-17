/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Crash Safety & Persistence Implementation
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#include "../include/nabd/persistence.h"
#include "../include/nabd/internal_impl.h"
#include "../include/nabd/types.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*
 * Get current time in nanoseconds
 */
static uint64_t get_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/*
 * Simple checksum for checkpoint
 */
static uint64_t compute_checksum(const nabd_checkpoint_t *ckpt) {
  uint64_t sum = ckpt->magic;
  sum ^= ckpt->timestamp;
  sum ^= ckpt->group_id;
  sum ^= ckpt->tail;
  sum = (sum << 13) | (sum >> 51); /* Rotate */
  return sum;
}

/*
 * Run diagnostic check on a queue
 */
int nabd_diagnose(const char *name, nabd_diagnostic_t *diag) {
  if (!name || !diag)
    return NABD_INVALID;

  memset(diag, 0, sizeof(*diag));
  diag->state = NABD_STATE_CORRUPTED;

  /* Try to open shared memory */
  int fd = shm_open(name, O_RDONLY, 0);
  if (fd < 0) {
    diag->state = NABD_STATE_INCOMPLETE;
    return NABD_NOTFOUND;
  }

  /* Map control block */
  void *ptr = mmap(NULL, sizeof(nabd_control_t), PROT_READ, MAP_SHARED, fd, 0);
  if (ptr == MAP_FAILED) {
    close(fd);
    diag->state = NABD_STATE_INCOMPLETE;
    return NABD_SYSERR;
  }

  nabd_control_t *ctrl = (nabd_control_t *)ptr;

  /* Check magic */
  diag->magic_ok = (ctrl->magic == NABD_MAGIC);
  if (!diag->magic_ok) {
    diag->state = NABD_STATE_CORRUPTED;
    munmap(ptr, sizeof(nabd_control_t));
    close(fd);
    return NABD_OK;
  }

  /* Check version */
  uint64_t expected_version = (NABD_VERSION_MAJOR << 16) | NABD_VERSION_MINOR;
  diag->version_ok = (ctrl->version == expected_version);
  if (!diag->version_ok) {
    diag->state = NABD_STATE_VERSION_ERR;
    munmap(ptr, sizeof(nabd_control_t));
    close(fd);
    return NABD_OK;
  }

  /* Extract state */
  diag->head = atomic_load(&ctrl->head);
  diag->tail = atomic_load(&ctrl->tail);
  diag->capacity = ctrl->capacity;
  diag->slot_size = ctrl->slot_size;

  /* Calculate pending */
  diag->pending = (diag->head >= diag->tail) ? (diag->head - diag->tail) : 0;

  /* Validate sanity */
  if (diag->pending > diag->capacity) {
    diag->state = NABD_STATE_CORRUPTED;
  } else if (diag->pending == 0) {
    diag->state = NABD_STATE_EMPTY;
  } else {
    diag->state = NABD_STATE_OK;
  }

  munmap(ptr, sizeof(nabd_control_t));
  close(fd);

  return NABD_OK;
}

/*
 * Attempt to recover a queue
 */
int nabd_recover(const char *name, int force) {
  if (!name)
    return NABD_INVALID;

  /* Run diagnostic first */
  nabd_diagnostic_t diag;
  int ret = nabd_diagnose(name, &diag);
  if (ret != NABD_OK) {
    return ret;
  }

  /* Check if recovery is needed/possible */
  if (diag.state == NABD_STATE_OK || diag.state == NABD_STATE_EMPTY) {
    return NABD_OK; /* No recovery needed */
  }

  if (diag.state == NABD_STATE_CORRUPTED && !force) {
    return NABD_CORRUPTED; /* Unrecoverable without force */
  }

  if (diag.state == NABD_STATE_INCOMPLETE) {
    /* Just unlink and let it be recreated */
    shm_unlink(name);
    return NABD_OK;
  }

  /* Force recovery: reset head and tail */
  if (force) {
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) {
      return NABD_SYSERR;
    }

    void *ptr = mmap(NULL, sizeof(nabd_control_t), PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
      close(fd);
      return NABD_SYSERR;
    }

    nabd_control_t *ctrl = (nabd_control_t *)ptr;

    /* Reset to empty state */
    uint64_t head = atomic_load(&ctrl->head);
    atomic_store(&ctrl->tail, head); /* Set tail = head (empty) */

    munmap(ptr, sizeof(nabd_control_t));
    close(fd);
  }

  return NABD_OK;
}

/*
 * Save consumer checkpoint
 */
int nabd_checkpoint_save(nabd_consumer_t *c, const char *filepath) {
  if (!c || !filepath)
    return NABD_INVALID;

  nabd_checkpoint_t ckpt;
  memset(&ckpt, 0, sizeof(ckpt));

  ckpt.magic = NABD_CHECKPOINT_MAGIC;
  ckpt.timestamp = get_time_ns();
  ckpt.group_id = c->group_id;
  ckpt.tail = atomic_load(&c->group->tail);
  ckpt.checksum = compute_checksum(&ckpt);

  FILE *f = fopen(filepath, "wb");
  if (!f) {
    return NABD_SYSERR;
  }

  size_t written = fwrite(&ckpt, sizeof(ckpt), 1, f);
  fclose(f);

  return (written == 1) ? NABD_OK : NABD_SYSERR;
}

/*
 * Load consumer checkpoint
 */
int nabd_checkpoint_load(const char *filepath, nabd_checkpoint_t *ckpt) {
  if (!filepath || !ckpt)
    return NABD_INVALID;

  FILE *f = fopen(filepath, "rb");
  if (!f) {
    return NABD_NOTFOUND;
  }

  size_t read = fread(ckpt, sizeof(*ckpt), 1, f);
  fclose(f);

  if (read != 1) {
    return NABD_CORRUPTED;
  }

  /* Validate magic */
  if (ckpt->magic != NABD_CHECKPOINT_MAGIC) {
    return NABD_CORRUPTED;
  }

  /* Verify checksum */
  uint64_t expected = compute_checksum(ckpt);
  if (ckpt->checksum != expected) {
    return NABD_CORRUPTED;
  }

  return NABD_OK;
}

/*
 * Resume consumer from checkpoint
 */
nabd_consumer_t *nabd_consumer_resume(nabd_t *q,
                                      const nabd_checkpoint_t *ckpt) {
  if (!q || !ckpt)
    return NULL;

  /* Create or join the consumer group */
  nabd_consumer_t *c = nabd_consumer_create(q, ckpt->group_id);
  if (!c) {
    /* Try joining existing */
    c = nabd_consumer_join(q, ckpt->group_id);
    if (!c)
      return NULL;
  }

  /* Restore tail position */
  uint64_t head = atomic_load(&q->ctrl->head);

  /* Validate checkpoint is not ahead of current head */
  if (ckpt->tail > head) {
    /* Checkpoint is ahead - start from current head instead */
    atomic_store(&c->group->tail, head);
  } else {
    /* Restore to checkpoint position */
    atomic_store(&c->group->tail, ckpt->tail);
  }

  return c;
}

/*
 * Get last activity timestamp (placeholder)
 */
uint64_t nabd_last_activity(nabd_t *q) {
  if (!q)
    return 0;

  /* For now, just return current time as we don't track activity */
  return get_time_ns();
}
