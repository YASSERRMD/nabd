/*
 * NABD - High-Performance Shared Memory IPC
 *
 * Internal Macros and Utilities
 *
 * Copyright (c) 2025 Mohamed Yasser
 * Licensed under MIT License
 */

#ifndef NABD_INTERNAL_H
#define NABD_INTERNAL_H

#include <stdatomic.h>
#include <stdint.h>

/*
 * ============================================================================
 * Cache Line and Alignment
 * ============================================================================
 */

/* Cache line size - 64 bytes for most modern CPUs */
#ifndef NABD_CACHE_LINE_SIZE
#define NABD_CACHE_LINE_SIZE 64
#endif

/* Force alignment to cache line boundary */
#define NABD_CACHE_ALIGNED __attribute__((aligned(NABD_CACHE_LINE_SIZE)))

/* Padding macro to fill remaining cache line bytes */
#define NABD_PAD_TO_CACHE_LINE(size)                                           \
  uint8_t _pad[NABD_CACHE_LINE_SIZE - ((size) % NABD_CACHE_LINE_SIZE)]

/*
 * ============================================================================
 * Compiler Hints
 * ============================================================================
 */

/* Branch prediction hints */
#if defined(__GNUC__) || defined(__clang__)
#define NABD_LIKELY(x) __builtin_expect(!!(x), 1)
#define NABD_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define NABD_LIKELY(x) (x)
#define NABD_UNLIKELY(x) (x)
#endif

/* Inline hints */
#define NABD_INLINE static inline __attribute__((always_inline))
#define NABD_NOINLINE __attribute__((noinline))

/* Unreachable code marker */
#define NABD_UNREACHABLE __builtin_unreachable()

/* Prefetch hints */
#if defined(__GNUC__) || defined(__clang__)
/* Read prefetch, low temporal locality */
#define NABD_PREFETCH_READ(addr) __builtin_prefetch((addr), 0, 0)
/* Write prefetch, low temporal locality */
#define NABD_PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 0)
/* Read prefetch, high temporal locality */
#define NABD_PREFETCH_READ_T0(addr) __builtin_prefetch((addr), 0, 3)
/* Write prefetch, high temporal locality */
#define NABD_PREFETCH_WRITE_T0(addr) __builtin_prefetch((addr), 1, 3)
#else
#define NABD_PREFETCH_READ(addr)
#define NABD_PREFETCH_WRITE(addr)
#define NABD_PREFETCH_READ_T0(addr)
#define NABD_PREFETCH_WRITE_T0(addr)
#endif

/*
 * ============================================================================
 * Memory Barriers
 * ============================================================================
 */

/* Full memory barrier */
#define NABD_BARRIER() atomic_thread_fence(memory_order_seq_cst)

/* Acquire barrier - ensures loads after this see stores before release */
#define NABD_ACQUIRE() atomic_thread_fence(memory_order_acquire)

/* Release barrier - ensures stores before this are visible after acquire */
#define NABD_RELEASE() atomic_thread_fence(memory_order_release)

/* Compiler barrier only - prevents compiler reordering */
#define NABD_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")

/*
 * ============================================================================
 * Atomic Operations with Explicit Ordering
 * ============================================================================
 */

/* Load with relaxed ordering (for own variable) */
#define NABD_LOAD_RELAXED(ptr) atomic_load_explicit((ptr), memory_order_relaxed)

/* Load with acquire ordering (to see other's writes) */
#define NABD_LOAD_ACQUIRE(ptr) atomic_load_explicit((ptr), memory_order_acquire)

/* Store with relaxed ordering */
#define NABD_STORE_RELAXED(ptr, val)                                           \
  atomic_store_explicit((ptr), (val), memory_order_relaxed)

/* Store with release ordering (to publish writes) */
#define NABD_STORE_RELEASE(ptr, val)                                           \
  atomic_store_explicit((ptr), (val), memory_order_release)

/* Compare-and-swap with acquire-release ordering */
#define NABD_CAS_ACQ_REL(ptr, expected, desired)                               \
  atomic_compare_exchange_weak_explicit((ptr), (expected), (desired),          \
                                        memory_order_acq_rel,                  \
                                        memory_order_acquire)

/*
 * ============================================================================
 * Power of Two Utilities
 * ============================================================================
 */

/* Check if n is a power of 2 */
NABD_INLINE int nabd_is_power_of_2(size_t n) { return n && !(n & (n - 1)); }

/* Round up to next power of 2 */
NABD_INLINE size_t nabd_next_power_of_2(size_t n) {
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
#if __SIZEOF_SIZE_T__ >= 8
  n |= n >> 32;
#endif
  n++;
  return n;
}

/* Fast modulo for power-of-2 divisor */
NABD_INLINE size_t nabd_mod_pow2(size_t n, size_t mask) { return n & mask; }

/*
 * ============================================================================
 * CPU Relaxation
 * ============================================================================
 */

/* CPU pause/yield for spin loops */
#if defined(__x86_64__) || defined(__i386__)
#define NABD_CPU_PAUSE() __asm__ __volatile__("pause" ::: "memory")
#elif defined(__aarch64__)
#define NABD_CPU_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
#define NABD_CPU_PAUSE() NABD_COMPILER_BARRIER()
#endif

/* Spin wait with backoff */
NABD_INLINE void nabd_spin_wait(int iterations) {
  for (int i = 0; i < iterations; i++) {
    NABD_CPU_PAUSE();
  }
}

/*
 * ============================================================================
 * Debug Helpers
 * ============================================================================
 */

#ifdef NABD_DEBUG
#include <stdio.h>
#define NABD_DBG(fmt, ...) fprintf(stderr, "[NABD] " fmt "\n", ##__VA_ARGS__)
#else
#define NABD_DBG(fmt, ...) ((void)0)
#endif

#endif /* NABD_INTERNAL_H */
