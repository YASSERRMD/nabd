# NABD Ring Buffer Protocol Specification

**Version:** 0.1  
**Status:** Draft

## 1. Overview

This document specifies the ring buffer protocol used by NABD for lock-free, zero-copy message passing between a single producer and single consumer.

## 2. Memory Model

### 2.1 Shared Memory Layout

```
┌─────────────────────────────────────────────────────────────┐
│                    SHARED MEMORY REGION                      │
├─────────────────────────────────────────────────────────────┤
│  Control Block (256 bytes)                                   │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ [0x00-0x3F]  Header: magic, version, capacity, etc.     ││
│  │ [0x40-0x7F]  Producer line: head (atomic)               ││
│  │ [0x80-0xBF]  Consumer line: tail (atomic)               ││
│  │ [0xC0-0xFF]  Reserved                                   ││
│  └─────────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│  Ring Buffer (capacity × slot_size bytes)                    │
│  ┌────────┬────────┬────────┬────────┬───────┬────────┐    │
│  │ Slot 0 │ Slot 1 │ Slot 2 │  ...   │Slot N-2│Slot N-1│    │
│  └────────┴────────┴────────┴────────┴───────┴────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Slot Structure

Each slot contains:

| Offset | Size | Field    | Description              |
|--------|------|----------|--------------------------|
| 0      | 2    | length   | Payload length           |
| 2      | 2    | flags    | Reserved                 |
| 4      | 4    | sequence | Sequence number (debug)  |
| 8      | N-8  | payload  | User data                |

## 3. Buffer State

### 3.1 Index Variables

- **head**: Next position for producer to write (atomic uint64)
- **tail**: Next position for consumer to read (atomic uint64)

Both are monotonically increasing and wrap using modulo operation:
```
physical_index = logical_index % capacity
```

### 3.2 State Conditions

| Condition | Meaning |
|-----------|---------|
| `head == tail` | Buffer empty |
| `head - tail == capacity` | Buffer full |
| `head - tail < capacity` | Space available |
| `head > tail` | Messages pending |

## 4. Operations

### 4.1 Push (Producer)

```
PUSH(data, len):
    1. head_local = atomic_load(&head, relaxed)
    2. tail_local = atomic_load(&tail, acquire)
    3. if (head_local - tail_local >= capacity):
           return NABD_FULL
    4. slot = buffer[head_local % capacity]
    5. memcpy(slot.payload, data, len)
    6. slot.length = len
    7. slot.sequence = head_local
    8. atomic_store(&head, head_local + 1, release)
    9. return NABD_OK
```

### 4.2 Pop (Consumer)

```
POP(buf, len):
    1. tail_local = atomic_load(&tail, relaxed)
    2. head_local = atomic_load(&head, acquire)
    3. if (tail_local == head_local):
           return NABD_EMPTY
    4. slot = buffer[tail_local % capacity]
    5. *len = slot.length
    6. memcpy(buf, slot.payload, slot.length)
    7. atomic_store(&tail, tail_local + 1, release)
    8. return NABD_OK
```

## 5. Memory Ordering

### 5.1 Required Orderings

| Operation | Order | Rationale |
|-----------|-------|-----------|
| Producer reads head | relaxed | Own variable, always fresh |
| Producer reads tail | acquire | Sync with consumer's release |
| Producer writes data | normal | Before head update |
| Producer writes head | release | Publish data |
| Consumer reads tail | relaxed | Own variable, always fresh |
| Consumer reads head | acquire | See producer's data |
| Consumer reads data | normal | After head acquire |
| Consumer writes tail | release | Signal consumption |

### 5.2 Correctness Argument

1. **Visibility**: Producer's `release` store to `head` synchronizes-with consumer's `acquire` load of `head`. This ensures consumer sees all writes to the slot.

2. **No data race**: Producer only writes to slots where `index >= head`. Consumer only reads from slots where `index < head && index >= tail`. These sets don't overlap when using proper ordering.

3. **Wrap-around safety**: Using 64-bit indices prevents wrap-around issues for practical lifetimes (>100 years at 1B msgs/sec).

## 6. Power-of-Two Optimization

Capacity must be a power of 2 to enable fast modulo:
```c
physical_index = logical_index & (capacity - 1)
```

## 7. Empty One-Slot Convention

We use the "waste one slot" approach to distinguish empty from full:
- Full: `head - tail == capacity`
- Empty: `head == tail`

This avoids the need for a separate count variable.

## 8. Error Handling

| Error | Cause | Producer Action | Consumer Action |
|-------|-------|-----------------|-----------------|
| FULL | No space | Yield/retry | N/A |
| EMPTY | No data | N/A | Yield/retry |
| TOOBIG | Message > slot_size | Split or error | Buffer too small |

## 9. Initialization Protocol

1. Producer creates shared memory with `shm_open` + `O_CREAT`
2. Producer sets size with `ftruncate`
3. Producer maps with `mmap`
4. Producer initializes control block (magic, version, indices = 0)
5. Consumer opens with `shm_open` (no O_CREAT)
6. Consumer maps with `mmap`
7. Consumer validates magic and version
