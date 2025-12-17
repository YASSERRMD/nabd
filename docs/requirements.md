# NABD Requirements Specification

**Version:** 0.1  
**Status:** Draft

## 1. Overview

NABD is a high-performance, lock-free, zero-copy shared memory ring buffer designed for ultra-fast inter-process communication (IPC) on a single node.

## 2. Goals & Constraints

### 2.1 Core Design Pillars

| Pillar | Description |
|--------|-------------|
| **Pure C** | No runtime, no hidden allocations, full control over memory layout |
| **Zero-Copy** | Messages live directly in shared memory, no memcpy in hot path |
| **Lock-Free** | Atomic operations only, no mutexes or condition variables |
| **Cache-Aware** | Cache-line aligned headers, false-sharing avoidance, explicit memory fences |
| **Single-Node** | Designed for colocated processes, not a network queue |

### 2.2 What NABD Is NOT

- Not Kafka
- Not Redis Streams
- Not a broker
- Not a networking system

**NABD is a primitive** — something other systems can build on top of.

## 3. Functional Requirements

### 3.1 Initial Scope (v0.1)

| Requirement | Description |
|-------------|-------------|
| FR-01 | Single producer support |
| FR-02 | Single consumer support |
| FR-03 | Fixed-size ring buffer |
| FR-04 | Shared memory via `shm_open` + `mmap` |
| FR-05 | Atomic head/tail offsets |
| FR-06 | Zero malloc in hot path |

### 3.2 Planned Extensions

| Requirement | Description | Priority |
|-------------|-------------|----------|
| FR-10 | Multiple consumers (fan-out) | P1 |
| FR-11 | Backpressure signaling | P1 |
| FR-12 | Crash-safe replay via persisted offsets | P2 |
| FR-13 | Optional durability layer | P2 |
| FR-14 | Metrics hooks (latency, drops, lag) | P1 |

## 4. Non-Functional Requirements

### 4.1 Performance Targets

| Metric | Target |
|--------|--------|
| Message throughput | > 10M msgs/sec (small messages) |
| Latency (p99) | < 1 μs |
| Memory overhead | Minimal (control block + buffer only) |

### 4.2 Reliability

- Must handle producer/consumer process restarts
- No data corruption on clean shutdown
- Graceful degradation under overload

### 4.3 Portability

- Primary: Linux
- Secondary: macOS, FreeBSD
- C11 standard compliance
- POSIX shared memory APIs

## 5. Risk Identification

| Risk | Severity | Mitigation |
|------|----------|------------|
| Memory fencing errors | High | Use proper acquire/release semantics, extensive testing |
| False sharing | Medium | Cache-line align all shared structures |
| ABA problem (multi-consumer) | High | Use sequence numbers or hazard pointers |
| Slow consumer blocking | Medium | Implement backpressure with timeouts |
| Shared memory cleanup | Low | Provide cleanup utilities, document recovery |

## 6. API Overview

```c
// Lifecycle
nabd_t* nabd_open(const char* name, size_t size, int flags);
int nabd_close(nabd_t* q);

// Producer
int nabd_push(nabd_t* q, const void* data, size_t len);

// Consumer  
int nabd_pop(nabd_t* q, void* buf, size_t* len);
```

## 7. Dependencies

- C11 compiler with `stdatomic.h`
- POSIX: `shm_open`, `mmap`, `ftruncate`
- No external runtime dependencies
