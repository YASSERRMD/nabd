/*
 * NABD Test Suite - API Unit Tests
 *
 * Tests all public API functions
 */

#include "../include/nabd/backpressure.h"
#include "../include/nabd/metrics.h"
#include "../include/nabd/nabd.h"
#include "../include/nabd/persistence.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE_NAME "/nabd_test"
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                         \
  do {                                                                         \
    printf("  Testing %s... ", #name);                                         \
    test_##name();                                                             \
    printf("OK\n");                                                            \
    passed++;                                                                  \
  } while (0)

static int passed = 0;

/* Cleanup before/after tests */
static void cleanup(void) { nabd_unlink(QUEUE_NAME); }

TEST(open_close) {
  cleanup();

  /* Create queue */
  nabd_t *q = nabd_open(QUEUE_NAME, 128, 256, NABD_CREATE | NABD_PRODUCER);
  assert(q != NULL);

  /* Get stats */
  nabd_stats_t stats;
  assert(nabd_stats(q, &stats) == NABD_OK);
  assert(stats.capacity == 128);
  assert(stats.head == 0);
  assert(stats.tail == 0);

  nabd_close(q);
  cleanup();
}

TEST(push_pop) {
  cleanup();

  nabd_t *p = nabd_open(QUEUE_NAME, 64, 128, NABD_CREATE | NABD_PRODUCER);
  nabd_t *c = nabd_open(QUEUE_NAME, 0, 0, NABD_CONSUMER);
  assert(p && c);

  /* Push message */
  const char *msg = "Hello NABD!";
  assert(nabd_push(p, msg, strlen(msg) + 1) == NABD_OK);

  /* Pop message */
  char buf[128];
  size_t len = sizeof(buf);
  assert(nabd_pop(c, buf, &len) == NABD_OK);
  assert(strcmp(buf, msg) == 0);

  /* Queue should be empty */
  assert(nabd_pop(c, buf, &len) == NABD_EMPTY);

  nabd_close(p);
  nabd_close(c);
  cleanup();
}

TEST(empty_full) {
  cleanup();

  nabd_t *q = nabd_open(QUEUE_NAME, 4, 64, NABD_CREATE | NABD_PRODUCER);
  assert(q);

  assert(nabd_empty(q) == 1);
  assert(nabd_full(q) == 0);

  /* Fill buffer */
  for (int i = 0; i < 4; i++) {
    int val = i;
    assert(nabd_push(q, &val, sizeof(val)) == NABD_OK);
  }

  assert(nabd_empty(q) == 0);
  assert(nabd_full(q) == 1);

  /* Try to push when full */
  int extra = 999;
  assert(nabd_push(q, &extra, sizeof(extra)) == NABD_FULL);

  nabd_close(q);
  cleanup();
}

TEST(peek_release) {
  cleanup();

  nabd_t *q = nabd_open(QUEUE_NAME, 16, 64,
                        NABD_CREATE | NABD_PRODUCER | NABD_CONSUMER);
  assert(q);

  int val = 42;
  assert(nabd_push(q, &val, sizeof(val)) == NABD_OK);

  /* Peek should see message */
  const void *data;
  size_t len;
  assert(nabd_peek(q, &data, &len) == NABD_OK);
  assert(*(int *)data == 42);

  /* Message should still be there */
  assert(nabd_peek(q, &data, &len) == NABD_OK);

  /* Release consumes message */
  assert(nabd_release(q) == NABD_OK);

  /* Now empty */
  assert(nabd_peek(q, &data, &len) == NABD_EMPTY);

  nabd_close(q);
  cleanup();
}

TEST(reserve_commit) {
  cleanup();

  nabd_t *q = nabd_open(QUEUE_NAME, 16, 64,
                        NABD_CREATE | NABD_PRODUCER | NABD_CONSUMER);
  assert(q);

  void *slot;
  assert(nabd_reserve(q, 10, &slot) == NABD_OK);

  /* Write directly to slot */
  memcpy(slot, "direct", 7);
  assert(nabd_commit(q, 7) == NABD_OK);

  /* Pop should get our data */
  char buf[64];
  size_t len = sizeof(buf);
  assert(nabd_pop(q, buf, &len) == NABD_OK);
  assert(strcmp(buf, "direct") == 0);

  nabd_close(q);
  cleanup();
}

TEST(metrics) {
  cleanup();

  nabd_t *q = nabd_open(QUEUE_NAME, 32, 64, NABD_CREATE | NABD_PRODUCER);
  assert(q);

  /* Push some messages */
  for (int i = 0; i < 10; i++) {
    nabd_push(q, &i, sizeof(i));
  }

  nabd_metrics_t metrics;
  assert(nabd_get_metrics(q, &metrics) == NABD_OK);
  assert(metrics.head == 10);
  assert(metrics.pending == 10);
  assert(metrics.fill_pct > 0);

  /* Test format */
  char buf[512];
  assert(nabd_format_metrics(&metrics, buf, sizeof(buf)) > 0);
  assert(nabd_format_metrics_json(&metrics, buf, sizeof(buf)) > 0);

  nabd_close(q);
  cleanup();
}

TEST(fill_level) {
  cleanup();

  nabd_t *q = nabd_open(QUEUE_NAME, 8, 64, NABD_CREATE | NABD_PRODUCER);
  assert(q);

  assert(nabd_fill_level(q) == 0);

  for (int i = 0; i < 4; i++) {
    nabd_push(q, &i, sizeof(i));
  }

  assert(nabd_fill_level(q) == 50);
  assert(nabd_is_pressured(q, 40) == 1);
  assert(nabd_is_pressured(q, 60) == 0);

  nabd_close(q);
  cleanup();
}

TEST(diagnose) {
  cleanup();

  nabd_t *q = nabd_open(QUEUE_NAME, 16, 64, NABD_CREATE | NABD_PRODUCER);
  assert(q);
  nabd_push(q, "test", 5);
  nabd_close(q);

  nabd_diagnostic_t diag;
  assert(nabd_diagnose(QUEUE_NAME, &diag) == NABD_OK);
  assert(diag.state == NABD_STATE_OK);
  assert(diag.magic_ok);
  assert(diag.version_ok);
  assert(diag.pending == 1);

  cleanup();
}

TEST(strerror) {
  assert(strcmp(nabd_strerror(NABD_OK), "Success") == 0);
  assert(strcmp(nabd_strerror(NABD_EMPTY), "Buffer empty") == 0);
  assert(strcmp(nabd_strerror(NABD_FULL), "Buffer full") == 0);
  assert(strcmp(nabd_strerror(-999), "Unknown error") == 0);
}

int main(void) {
  printf("NABD API Unit Tests\n");
  printf("===================\n\n");

  RUN_TEST(open_close);
  RUN_TEST(push_pop);
  RUN_TEST(empty_full);
  RUN_TEST(peek_release);
  RUN_TEST(reserve_commit);
  RUN_TEST(metrics);
  RUN_TEST(fill_level);
  RUN_TEST(diagnose);
  RUN_TEST(strerror);

  printf("\n%d tests passed!\n", passed);
  cleanup();

  return 0;
}
