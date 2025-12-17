/*
 * NABD Test Suite - Concurrency Stress Tests
 *
 * Tests multi-process producer/consumer scenarios
 */

#include "../include/nabd/nabd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define QUEUE_NAME "/nabd_stress"
#define NUM_MESSAGES 10000

static void cleanup(void) { nabd_unlink(QUEUE_NAME); }

/* Test: Single producer, single consumer with fork */
static void test_spsc_fork(void) {
  printf("  Testing SPSC with fork... ");
  cleanup();

  nabd_t *q = nabd_open(QUEUE_NAME, 1024, 128, NABD_CREATE | NABD_PRODUCER);
  assert(q);

  pid_t pid = fork();

  if (pid == 0) {
    /* Child: Consumer */
    nabd_close(q);

    nabd_t *cq = nabd_open(QUEUE_NAME, 0, 0, NABD_CONSUMER);
    assert(cq);

    int received = 0;
    int expected = 0;
    char buf[128];

    while (received < NUM_MESSAGES) {
      size_t len = sizeof(buf);
      int ret = nabd_pop(cq, buf, &len);

      if (ret == NABD_OK) {
        int val = *(int *)buf;
        if (val != expected) {
          fprintf(stderr, "Order mismatch: got %d, expected %d\n", val,
                  expected);
          exit(1);
        }
        expected++;
        received++;
      } else if (ret == NABD_EMPTY) {
        usleep(10);
      } else {
        fprintf(stderr, "Pop error: %s\n", nabd_strerror(ret));
        exit(1);
      }
    }

    nabd_close(cq);
    exit(0);
  }

  /* Parent: Producer */
  for (int i = 0; i < NUM_MESSAGES; i++) {
    while (nabd_push(q, &i, sizeof(i)) == NABD_FULL) {
      usleep(10);
    }
  }

  /* Wait for consumer */
  int status;
  waitpid(pid, &status, 0);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  nabd_close(q);
  cleanup();
  printf("OK\n");
}

/* Test: Rapid push/pop cycling */
static void test_rapid_cycle(void) {
  printf("  Testing rapid push/pop cycle... ");
  cleanup();

  nabd_t *q = nabd_open(QUEUE_NAME, 16, 64,
                        NABD_CREATE | NABD_PRODUCER | NABD_CONSUMER);
  assert(q);

  for (int i = 0; i < 10000; i++) {
    int val = i;
    assert(nabd_push(q, &val, sizeof(val)) == NABD_OK);

    int out;
    size_t len = sizeof(out);
    assert(nabd_pop(q, &out, &len) == NABD_OK);
    assert(out == val);
  }

  /* Should be empty */
  assert(nabd_empty(q) == 1);

  nabd_close(q);
  cleanup();
  printf("OK\n");
}

/* Test: Buffer wraparound */
static void test_wraparound(void) {
  printf("  Testing buffer wraparound... ");
  cleanup();

  nabd_t *q =
      nabd_open(QUEUE_NAME, 8, 64, NABD_CREATE | NABD_PRODUCER | NABD_CONSUMER);
  assert(q);

  /* Push/pop many times to force wraparound */
  for (int round = 0; round < 100; round++) {
    /* Fill partially */
    for (int i = 0; i < 4; i++) {
      int val = round * 100 + i;
      assert(nabd_push(q, &val, sizeof(val)) == NABD_OK);
    }

    /* Drain partially */
    for (int i = 0; i < 4; i++) {
      int out;
      size_t len = sizeof(out);
      assert(nabd_pop(q, &out, &len) == NABD_OK);
      assert(out == round * 100 + i);
    }
  }

  assert(nabd_empty(q) == 1);

  nabd_close(q);
  cleanup();
  printf("OK\n");
}

/* Test: Fill and drain */
static void test_fill_drain(void) {
  printf("  Testing fill and drain... ");
  cleanup();

  const int capacity = 64;
  nabd_t *q = nabd_open(QUEUE_NAME, capacity, 32,
                        NABD_CREATE | NABD_PRODUCER | NABD_CONSUMER);
  assert(q);

  /* Fill completely */
  for (int i = 0; i < capacity; i++) {
    assert(nabd_push(q, &i, sizeof(i)) == NABD_OK);
  }
  assert(nabd_full(q) == 1);

  /* Drain completely */
  for (int i = 0; i < capacity; i++) {
    int out;
    size_t len = sizeof(out);
    assert(nabd_pop(q, &out, &len) == NABD_OK);
    assert(out == i);
  }
  assert(nabd_empty(q) == 1);

  nabd_close(q);
  cleanup();
  printf("OK\n");
}

int main(void) {
  printf("NABD Concurrency Tests\n");
  printf("======================\n\n");

  test_rapid_cycle();
  test_wraparound();
  test_fill_drain();
  test_spsc_fork();

  printf("\nAll concurrency tests passed!\n");
  return 0;
}
