# NABD - Not A Broker Daemon

**NABD** is a high-performance, lock-free, zero-copy shared memory IPC primitive for modern systems. It allows processes to communicate with ultra-low latency (< 100ns) on a single node without the overhead of sockets, syscalls in the hot path, or serialization.

## Key Features

- **⚡️ Zero-Copy**: Messages are written directly into shared memory. Zero allocations in the hot path.
- **🔒 Lock-Free**: Uses C11 atomic operations for wait-free SPSC and lock-free SPMC optimization.
- **🚀 High Performance**: Designed for millions of ops/sec with sub-microsecond latency.
- **💾 Cache-Aware**: Cache-line aligned control blocks to eliminate false sharing.
- **👥 Multi-Consumer**: Supports Single-Producer/Multi-Consumer (SPMC) with up to 16 consumer groups.
- **📦 Persistence**: Consumer offset checkpointing for crash recovery and restartability.
- **🔭 Observability**: Built-in metrics for fill level, throughput, and deep diagnostics.
- **🛑 Backpressure**: Configurable flow control with buffer-full callbacks and exponential backoff.
- **🌍 Ecosystem**: First-class bindings for Python, Go, and Rust.

## Quick Start

### 1. Build

```bash
make
```

### 2. Run Examples

**Producer:**
```bash
./build/simple_producer
```

**Consumer:**
```bash
./build/simple_consumer
```

### 3. Usage Code

**Producer:**
```c
#include <nabd/nabd.h>

int main() {
    // Create queue with 1024 slots, 4KB per slot
    nabd_t* q = nabd_open("/myqueue", 1024, 4096, NABD_CREATE | NABD_PRODUCER);
    
    // Zero-copy write
    void* slot;
    if (nabd_reserve(q, 100, &slot) == NABD_OK) {
        sprintf(slot, "Hello Direct Memory!");
        nabd_commit(q, 20); // Commit 20 bytes
    }
    
    nabd_close(q);
}
```

**Consumer:**
```c
#include <nabd/nabd.h>

int main() {
    // Join existing queue as consumer
    nabd_t* q = nabd_open("/myqueue", 0, 0, NABD_CONSUMER);
    
    char buf[4096];
    size_t len = sizeof(buf);
    
    while (1) {
        if (nabd_pop(q, buf, &len) == NABD_OK) {
            printf("Received: %s\n", buf);
        }
    }
}
```

## Architecture

NABD uses a ring buffer in POSIX shared memory (`/dev/shm`). 
- **Control Block**: The first 256 bytes contain the `head` and `tail` pointers, aligned to separate cache lines (64 bytes) to prevent coherence ping-pong.
- **Slots**: The rest of the memory is divided into fixed-size slots.
- **Memory Ordering**: Uses `memory_order_acquire` and `memory_order_release` to ensure data visibility without locks.

## Multi-Consumer (SPMC)

NABD supports multiple consumer groups. each group tracks its own `tail`. The ring buffer slots are only reused when *all* active consumer groups have read them.

```c
// Create/Join a specific consumer group (ID: 1)
nabd_consumer_t* c = nabd_consumer_join(q, 1);
nabd_consumer_pop(c, buf, &len);
```

## Performance

On a MacBook Air M4 (2024):
- **Latency**: ~45 ns per message
- **Throughput**: > 22 Million msgs/sec (64-byte payload)
- **Bandwidth**: > 1.3 GB/s

Run the benchmark:
```bash
make run-bench
```

## Language Bindings

NABD includes bindings for popular languages in `bindings/`.

### Python
```bash
python3 bindings/python/nabd.py
```

### Go
```bash
cd bindings/go && go test -v
```

### Rust
```bash
cd bindings/rust && cargo test
```

## License

MIT License
