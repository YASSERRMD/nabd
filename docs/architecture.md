# NABD High-Level Architecture

## 1. System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                     SHARED MEMORY REGION                        │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    CONTROL BLOCK                         │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │   │
│  │  │   HEAD   │  │   TAIL   │  │   SIZE   │  │  FLAGS   │ │   │
│  │  │ (atomic) │  │ (atomic) │  │          │  │          │ │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘ │   │
│  │                    [64-byte cache-line aligned]          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    RING BUFFER                           │   │
│  │  ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐    │   │
│  │  │ S0 │ S1 │ S2 │ S3 │ S4 │ S5 │ S6 │ S7 │... │ Sn │    │   │
│  │  └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘    │   │
│  │         ↑                              ↑                 │   │
│  │       TAIL                           HEAD                │   │
│  │     (consumer)                     (producer)            │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
         ↑                                           ↑
         │                                           │
    ┌────┴────┐                               ┌──────┴──────┐
    │ CONSUMER │                               │  PRODUCER   │
    │ PROCESS  │                               │   PROCESS   │
    └──────────┘                               └─────────────┘
```

## 2. Memory Layout

### 2.1 Control Block Structure

```
Offset    Size    Field           Description
──────────────────────────────────────────────────
0x00      8       magic           Magic number for validation
0x08      8       version         Protocol version
0x10      8       head            Producer write position (atomic)
0x18      8       tail            Consumer read position (atomic)
0x20      8       capacity        Ring buffer capacity
0x28      8       slot_size       Size of each slot
0x30      16      reserved        Future extensions
0x40      -       (padding)       Align to 64-byte cache line
──────────────────────────────────────────────────
0x40+     -       ring_buffer     Start of ring buffer slots
```

### 2.2 Slot Structure

```
┌────────────────────────────────────────┐
│  SLOT HEADER (8 bytes)                 │
│  ┌──────────┬──────────┬────────────┐  │
│  │ length   │ flags    │ sequence   │  │
│  │ (2B)     │ (2B)     │ (4B)       │  │
│  └──────────┴──────────┴────────────┘  │
├────────────────────────────────────────┤
│  PAYLOAD (slot_size - 8 bytes)         │
│  ┌──────────────────────────────────┐  │
│  │  user data...                    │  │
│  └──────────────────────────────────┘  │
└────────────────────────────────────────┘
```

## 3. Operation Flow

### 3.1 Push (Producer)

```
1. Load current head (relaxed)
2. Check if buffer full: (head - tail) >= capacity
   If full → return NABD_FULL
3. Write data to slot[head % capacity]
4. Store head + 1 (release)
```

### 3.2 Pop (Consumer)

```
1. Load current tail (relaxed)
2. Load head (acquire)
3. Check if buffer empty: tail == head
   If empty → return NABD_EMPTY
4. Read data from slot[tail % capacity]
5. Store tail + 1 (release)
```

## 4. Memory Ordering

| Operation | Memory Order | Rationale |
|-----------|--------------|-----------|
| Load head (producer) | Relaxed | Own variable |
| Load tail (producer check) | Acquire | Sync with consumer |
| Store head | Release | Publish data |
| Load tail (consumer) | Relaxed | Own variable |
| Load head (consumer check) | Acquire | See producer's data |
| Store tail | Release | Signal consumption |

## 5. Cache-Line Considerations

- Control block head/tail padded to separate cache lines
- Avoids false sharing between producer and consumer
- Each slot aligned for optimal access

```c
struct nabd_control {
    uint64_t magic;
    uint64_t version;
    _Alignas(64) _Atomic uint64_t head;  // Separate cache line
    _Alignas(64) _Atomic uint64_t tail;  // Separate cache line
    uint64_t capacity;
    uint64_t slot_size;
};
```

## 6. Component Diagram

```
┌──────────────────────────────────────────────────────────┐
│                       NABD LIBRARY                        │
├──────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐       │
│  │   PUBLIC    │  │   RING      │  │   SHARED    │       │
│  │    API      │──│   BUFFER    │──│   MEMORY    │       │
│  │             │  │   ENGINE    │  │   MANAGER   │       │
│  └─────────────┘  └─────────────┘  └─────────────┘       │
│         │                │                │               │
│         └────────────────┼────────────────┘               │
│                          │                                │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐       │
│  │  BACKPRES-  │  │   METRICS   │  │  PERSIST-   │       │
│  │    SURE     │  │   HOOKS     │  │    ENCE     │       │
│  │  (future)   │  │  (future)   │  │  (future)   │       │
│  └─────────────┘  └─────────────┘  └─────────────┘       │
└──────────────────────────────────────────────────────────┘
```

## 7. File Structure

```
nabd/
├── include/
│   └── nabd/
│       ├── nabd.h          # Public API
│       ├── types.h         # Type definitions
│       └── internal.h      # Internal macros
├── src/
│   ├── nabd.c              # Core implementation
│   ├── shm.c               # Shared memory management
│   ├── backpressure.c      # Backpressure (future)
│   ├── metrics.c           # Metrics (future)
│   └── persistence.c       # Persistence (future)
├── tests/
├── examples/
├── benchmarks/
└── docs/
```
