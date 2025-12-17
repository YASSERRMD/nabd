# NABD API Reference

This document details the public API for NABD.

## Include

```c
#include <nabd/nabd.h>
#include <nabd/backpressure.h>
#include <nabd/metrics.h>
#include <nabd/persistence.h>
```

---

## Core Lifecycle

### `nabd_open`

```c
nabd_t *nabd_open(const char *name, size_t capacity, size_t slot_size, int flags);
```

Opens or creates a shared memory queue.

- **name**: Logical name of the queue (e.g., "/myqueue").
- **capacity**: Number of slots (must be power of 2, e.g., 1024).
- **slot_size**: Size of each slot in bytes.
- **flags**: Bitmask of:
  - `NABD_CREATE`: Create if not exists.
  - `NABD_PRODUCER`: Enable producer operations.
  - `NABD_CONSUMER`: Enable consumer operations.
- **Returns**: `nabd_t*` handle on success, `NULL` on failure.

### `nabd_close`

```c
int nabd_close(nabd_t *q);
```

Closes the queue handle and unmaps memory. Does NOT delete the shared memory segment.

### `nabd_unlink`

```c
int nabd_unlink(const char *name);
```

Removes the shared memory object from the system. Data is lost once all processes close it.

---

## Producer Operations

### `nabd_push`

```c
int nabd_push(nabd_t *q, const void *data, size_t len);
```

Copies data into the queue.

- **Returns**:
  - `NABD_OK`: Success.
  - `NABD_FULL`: Buffer full.
  - `NABD_TOOBIG`: Message larger than slot size.

### `nabd_reserve` & `nabd_commit` (Zero-Copy)

```c
int nabd_reserve(nabd_t *q, size_t len, void **slot);
int nabd_commit(nabd_t *q, size_t len);
```

1. **reserve**: Gets a pointer to the next write slot.
2. **Write data directly** to `*slot`.
3. **commit**: Publishing the record to consumers.

---

## Consumer Operations

### `nabd_pop`

```c
int nabd_pop(nabd_t *q, void *buf, size_t *len);
```

Copies data from the queue to `buf`. *Single-consumer mode only*.

### `nabd_peek` & `nabd_release` (Zero-Copy)

```c
int nabd_peek(nabd_t *q, const void **data, size_t *len);
int nabd_release(nabd_t *q);
```

1. **peek**: Gets a pointer to the next read message.
2. **Read data**.
3. **release**: Marks the slot as free.

---

## Multi-Consumer (SPMC)

### `nabd_consumer_create` / `join`

```c
nabd_consumer_t* nabd_consumer_create(nabd_t* q, uint32_t group_id);
nabd_consumer_t* nabd_consumer_join(nabd_t* q, uint32_t group_id);
```

Creates or joins a consumer group. Each group has an independent read position.

### `nabd_consumer_pop`

```c
int nabd_consumer_pop(nabd_consumer_t* c, void* buf, size_t* len);
```

Pops a message for a specific consumer group.

---

## Observability

### `nabd_get_metrics`

```c
int nabd_get_metrics(nabd_t* q, nabd_metrics_t* metrics);
```

Retrieves detailed statistics:
- `head`, `tail`
- `fill_pct`
- `pending` messages

### `nabd_diagnose`

```c
int nabd_diagnose(const char* name, nabd_diagnostic_t* diag);
```

Checks health of a queue (even if corrupted) and returns state:
- `NABD_STATE_OK`
- `NABD_STATE_CORRUPTED`
- `NABD_STATE_EMPTY`

---

## Constants & Error Codes

| Code | Value | Description |
|------|-------|-------------|
| `NABD_OK` | 0 | Success |
| `NABD_EMPTY` | -1 | Buffer empty |
| `NABD_FULL` | -2 | Buffer full |
| `NABD_TOOBIG` | -7 | Message too large |
