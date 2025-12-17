/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Crash Safety & Persistence
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#ifndef NABD_PERSISTENCE_H
#define NABD_PERSISTENCE_H

#include "nabd.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Diagnostic Information
 * ============================================================================
 */

/**
 * Queue state after diagnostic check
 */
typedef enum {
  NABD_STATE_OK,         /* Queue is healthy */
  NABD_STATE_EMPTY,      /* Queue is empty (normal) */
  NABD_STATE_CORRUPTED,  /* Corruption detected */
  NABD_STATE_STALE,      /* Appears abandoned (no recent activity) */
  NABD_STATE_INCOMPLETE, /* Incomplete initialization */
  NABD_STATE_VERSION_ERR /* Version mismatch */
} nabd_state_t;

/**
 * Diagnostic result structure
 */
typedef struct {
  nabd_state_t state;  /* Overall state */
  uint64_t head;       /* Current head position */
  uint64_t tail;       /* Current tail position */
  uint64_t pending;    /* Messages pending (head - tail) */
  uint32_t magic_ok;   /* Magic number valid */
  uint32_t version_ok; /* Version compatible */
  uint64_t capacity;   /* Queue capacity */
  uint64_t slot_size;  /* Slot size */
} nabd_diagnostic_t;

/*
 * ============================================================================
 * Checkpoint Structure
 * ============================================================================
 */

/**
 * Consumer checkpoint for crash recovery
 */
typedef struct {
  uint64_t magic;     /* Checkpoint magic */
  uint64_t timestamp; /* Checkpoint time (ns) */
  uint32_t group_id;  /* Consumer group ID */
  uint32_t reserved;  /* Padding */
  uint64_t tail;      /* Last committed tail position */
  uint64_t checksum;  /* Simple checksum */
} nabd_checkpoint_t;

#define NABD_CHECKPOINT_MAGIC 0x434B5054414244ULL /* "NABDCKPT" */

/*
 * ============================================================================
 * Persistence API
 * ============================================================================
 */

/**
 * Run diagnostic check on a queue
 *
 * @param name      Shared memory name
 * @param diag      Output: diagnostic information
 *
 * @return NABD_OK on success (check diag->state for result)
 */
int nabd_diagnose(const char *name, nabd_diagnostic_t *diag);

/**
 * Attempt to recover a potentially corrupted queue
 *
 * @param name        Shared memory name
 * @param force       If true, reset even if corruption detected
 *
 * @return NABD_OK on success
 *         NABD_CORRUPTED if unrecoverable
 */
int nabd_recover(const char *name, int force);

/**
 * Save consumer checkpoint to file
 *
 * @param c         Consumer handle
 * @param filepath  Path to checkpoint file
 *
 * @return NABD_OK on success
 */
int nabd_checkpoint_save(nabd_consumer_t *c, const char *filepath);

/**
 * Load consumer checkpoint from file
 *
 * @param filepath  Path to checkpoint file
 * @param ckpt      Output: checkpoint data
 *
 * @return NABD_OK on success
 *         NABD_NOTFOUND if file doesn't exist
 *         NABD_CORRUPTED if checksum fails
 */
int nabd_checkpoint_load(const char *filepath, nabd_checkpoint_t *ckpt);

/**
 * Resume consumer from checkpoint
 *
 * @param q         Queue handle
 * @param ckpt      Checkpoint to resume from
 *
 * @return Consumer handle on success, NULL on failure
 */
nabd_consumer_t *nabd_consumer_resume(nabd_t *q, const nabd_checkpoint_t *ckpt);

/**
 * Get last activity timestamp for a queue
 *
 * @param q  Queue handle
 *
 * @return Timestamp in nanoseconds, 0 on error
 */
uint64_t nabd_last_activity(nabd_t *q);

#ifdef __cplusplus
}
#endif

#endif /* NABD_PERSISTENCE_H */
