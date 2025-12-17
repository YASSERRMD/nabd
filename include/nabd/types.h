/*
 * NABD - High-Performance Shared Memory IPC
 * 
 * Type Definitions
 * 
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#ifndef NABD_TYPES_H
#define NABD_TYPES_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdalign.h>

/*
 * Cache line size for alignment
 * Most modern x86/ARM processors use 64-byte cache lines
 */
#define NABD_CACHE_LINE_SIZE 64

/*
 * Magic number for shared memory validation
 * ASCII: "NABD" + version marker
 */
#define NABD_MAGIC 0x4442414E00010000ULL  /* "NABD" + v1.0 */

/*
 * Protocol version
 */
#define NABD_VERSION_MAJOR 0
#define NABD_VERSION_MINOR 1

/*
 * Default configuration
 */
#define NABD_DEFAULT_SLOT_SIZE 4096
#define NABD_DEFAULT_CAPACITY  1024

/*
 * Flags for nabd_open
 */
#define NABD_CREATE   0x01  /* Create new shared memory region */
#define NABD_PRODUCER 0x02  /* Open as producer */
#define NABD_CONSUMER 0x04  /* Open as consumer */

/*
 * Error codes
 */
typedef enum {
    NABD_OK          =  0,  /* Success */
    NABD_EMPTY       = -1,  /* Buffer is empty (pop) */
    NABD_FULL        = -2,  /* Buffer is full (push) */
    NABD_NOMEM       = -3,  /* Out of memory */
    NABD_INVALID     = -4,  /* Invalid argument */
    NABD_EXISTS      = -5,  /* Already exists */
    NABD_NOTFOUND    = -6,  /* Not found */
    NABD_TOOBIG      = -7,  /* Message too large */
    NABD_CORRUPTED   = -8,  /* Data corruption detected */
    NABD_VERSION     = -9,  /* Version mismatch */
    NABD_PERMISSION  = -10, /* Permission denied */
    NABD_SYSERR      = -11  /* System error (check errno) */
} nabd_error_t;

/*
 * Slot header - prepended to each message in the ring buffer
 * 
 * Layout:
 *   [0:1]  length   - payload length (max 65535 bytes)
 *   [2:3]  flags    - reserved for future use
 *   [4:7]  sequence - sequence number for debugging
 */
typedef struct {
    uint16_t length;    /* Payload length */
    uint16_t flags;     /* Reserved flags */
    uint32_t sequence;  /* Sequence number */
} nabd_slot_header_t;

/*
 * Control block - located at the start of shared memory
 * 
 * Cache-line aligned to avoid false sharing between producer and consumer.
 * Head and tail are on separate cache lines.
 */
typedef struct {
    /* Immutable after creation - shared cache line */
    uint64_t magic;       /* Magic number for validation */
    uint64_t version;     /* Protocol version */
    uint64_t capacity;    /* Number of slots */
    uint64_t slot_size;   /* Bytes per slot (including header) */
    uint64_t buffer_offset; /* Offset to ring buffer start */
    uint64_t reserved[3]; /* Future extensions */
    
    /* Producer cache line - only producer writes here */
    alignas(NABD_CACHE_LINE_SIZE) 
    _Atomic uint64_t head;  /* Next write position */
    uint64_t head_pad[7];   /* Padding to fill cache line */
    
    /* Consumer cache line - only consumer writes here */
    alignas(NABD_CACHE_LINE_SIZE)
    _Atomic uint64_t tail;  /* Next read position */
    uint64_t tail_pad[7];   /* Padding to fill cache line */
    
} nabd_control_t;

/*
 * Static assertion to verify control block layout
 */
_Static_assert(sizeof(nabd_control_t) == 256, 
               "Control block must be 256 bytes");
_Static_assert(offsetof(nabd_control_t, head) % NABD_CACHE_LINE_SIZE == 0,
               "Head must be cache-line aligned");
_Static_assert(offsetof(nabd_control_t, tail) % NABD_CACHE_LINE_SIZE == 0,
               "Tail must be cache-line aligned");

/*
 * NABD handle - opaque structure for users
 * Internal implementation details are hidden
 */
typedef struct nabd nabd_t;

/*
 * Statistics structure for monitoring
 */
typedef struct {
    uint64_t head;         /* Current head position */
    uint64_t tail;         /* Current tail position */
    uint64_t capacity;     /* Total slots */
    uint64_t used;         /* Slots currently in use */
    uint64_t slot_size;    /* Bytes per slot */
} nabd_stats_t;

#endif /* NABD_TYPES_H */
