/*
 * NABD Benchmark - Latency and Throughput
 *
 * Measures round-trip latency and throughput
 */

#include "../include/nabd/nabd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_NAME "/nabd_bench"
#define WARMUP_MSGS 1000
#define BENCH_MSGS 100000
#define MSG_SIZE 64

/* Get time in nanoseconds */
static inline uint64_t get_nanos(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(int argc, char *argv[]) {
  printf("NABD Latency/Throughput Benchmark\n");
  printf("==================================\n\n");

  int msg_count = BENCH_MSGS;
  int msg_size = MSG_SIZE;

  if (argc > 1)
    msg_count = atoi(argv[1]);
  if (argc > 2)
    msg_size = atoi(argv[2]);

  printf("Configuration:\n");
  printf("  Messages:     %d\n", msg_count);
  printf("  Message size: %d bytes\n", msg_size);
  printf("\n");

  /* Create queue */
  nabd_t *q =
      nabd_open(QUEUE_NAME, 8192, msg_size + 64, NABD_CREATE | NABD_PRODUCER);
  if (!q) {
    perror("nabd_open");
    return 1;
  }

  /* Fork consumer */
  pid_t pid = fork();

  if (pid == 0) {
    /* Child: Consumer */
    nabd_close(q);

    nabd_t *cq = nabd_open(QUEUE_NAME, 0, 0, NABD_CONSUMER);
    if (!cq) {
      perror("consumer: nabd_open");
      exit(1);
    }

    char buf[4096];
    size_t len;
    int received = 0;

    while (received < msg_count) {
      len = sizeof(buf);
      int ret = nabd_pop(cq, buf, &len);

      if (ret == NABD_OK) {
        received++;
      } else if (ret == NABD_EMPTY) {
        /* Spin */
      } else {
        fprintf(stderr, "consumer error: %s\n", nabd_strerror(ret));
        break;
      }
    }

    nabd_close(cq);
    exit(0);
  }

  /* Parent: Producer */
  usleep(10000); /* Let consumer start */

  char *msg = malloc(msg_size);
  memset(msg, 'X', msg_size);

  /* Warmup */
  printf("Warming up (%d messages)...\n", WARMUP_MSGS);
  for (int i = 0; i < WARMUP_MSGS; i++) {
    while (nabd_push(q, msg, msg_size) == NABD_FULL) {
      /* Spin */
    }
  }

  /* Wait for warmup to complete */
  usleep(100000);

  /* Clear queue */
  nabd_close(q);
  nabd_unlink(QUEUE_NAME);

  q = nabd_open(QUEUE_NAME, 8192, msg_size + 64, NABD_CREATE | NABD_PRODUCER);
  if (!q) {
    perror("nabd_open (reset)");
    return 1;
  }

  /* Re-fork consumer */
  wait(NULL); /* Wait for old consumer */

  pid = fork();
  if (pid == 0) {
    nabd_close(q);

    nabd_t *cq = nabd_open(QUEUE_NAME, 0, 0, NABD_CONSUMER);
    if (!cq)
      exit(1);

    char buf[4096];
    size_t len;
    int received = 0;

    while (received < msg_count) {
      len = sizeof(buf);
      if (nabd_pop(cq, buf, &len) == NABD_OK) {
        received++;
      }
    }

    nabd_close(cq);
    exit(0);
  }

  usleep(10000);

  /* Benchmark */
  printf("Benchmarking (%d messages)...\n", msg_count);

  uint64_t start = get_nanos();

  for (int i = 0; i < msg_count; i++) {
    while (nabd_push(q, msg, msg_size) == NABD_FULL) {
      /* Spin */
    }
  }

  /* Wait for consumer */
  wait(NULL);

  uint64_t end = get_nanos();
  uint64_t elapsed_ns = end - start;

  /* Calculate metrics */
  double elapsed_sec = elapsed_ns / 1e9;
  double throughput = msg_count / elapsed_sec;
  double avg_latency_ns = (double)elapsed_ns / msg_count;
  double bandwidth_mbs = (throughput * msg_size) / (1024 * 1024);

  printf("\n");
  printf("Results:\n");
  printf("  Elapsed time:  %.3f seconds\n", elapsed_sec);
  printf("  Throughput:    %.2f msgs/sec\n", throughput);
  printf("  Avg latency:   %.2f ns/msg\n", avg_latency_ns);
  printf("  Bandwidth:     %.2f MB/s\n", bandwidth_mbs);

  if (throughput > 1000000) {
    printf("\n  ✓ Achieved %.2f M msgs/sec\n", throughput / 1e6);
  }

  free(msg);
  nabd_close(q);
  nabd_unlink(QUEUE_NAME);

  return 0;
}
