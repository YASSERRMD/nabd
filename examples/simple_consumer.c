/*
 * NABD Example - Simple Consumer
 *
 * Demonstrates basic message consumption
 */

#include "../include/nabd/nabd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_NAME "/nabd_example"

int main(int argc, char *argv[]) {
  printf("NABD Simple Consumer\n");
  printf("====================\n\n");

  /* Open existing queue */
  nabd_t *q = nabd_open(QUEUE_NAME, 0, 0, NABD_CONSUMER);
  if (!q) {
    perror("nabd_open");
    printf("Make sure producer is running first!\n");
    return 1;
  }

  printf("Connected to queue: %s\n\n", QUEUE_NAME);

  char buf[256];
  size_t len;
  int received = 0;
  int empty_count = 0;
  int max_empty = 1000; /* Exit after 1000 consecutive empty reads */

  while (empty_count < max_empty) {
    len = sizeof(buf);
    int ret = nabd_pop(q, buf, &len);

    if (ret == NABD_OK) {
      received++;
      empty_count = 0;

      if (received <= 5 || (received % 20 == 0)) {
        printf("[%03d] Received (%zu bytes): %s\n", received, len, buf);
      } else if (received == 6) {
        printf("...\n");
      }
    } else if (ret == NABD_EMPTY) {
      empty_count++;
      usleep(1000); /* Wait 1ms before retry */
    } else {
      printf("Error: %s\n", nabd_strerror(ret));
      break;
    }
  }

  printf("\nConsumer finished:\n");
  printf("  Messages received: %d\n", received);

  /* Get stats */
  nabd_stats_t stats;
  if (nabd_stats(q, &stats) == NABD_OK) {
    printf("\nQueue stats:\n");
    printf("  Head: %llu\n", (unsigned long long)stats.head);
    printf("  Tail: %llu\n", (unsigned long long)stats.tail);
    printf("  Used: %llu/%llu\n", (unsigned long long)stats.used,
           (unsigned long long)stats.capacity);
  }

  nabd_close(q);
  printf("Done.\n");

  return 0;
}
