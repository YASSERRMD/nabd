/*
 * NABD Example - Simple Producer
 *
 * Demonstrates basic message publishing
 */

#include "../include/nabd/nabd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_NAME "/nabd_example"
#define NUM_MESSAGES 100

int main(int argc, char *argv[]) {
  printf("NABD Simple Producer\n");
  printf("====================\n\n");

  /* Create queue */
  nabd_t *q = nabd_open(QUEUE_NAME, 1024, 256, NABD_CREATE | NABD_PRODUCER);
  if (!q) {
    perror("nabd_open");
    return 1;
  }

  printf("Created queue: %s\n", QUEUE_NAME);
  printf("Sending %d messages...\n\n", NUM_MESSAGES);

  char msg[128];
  int sent = 0;
  int full_count = 0;

  for (int i = 0; i < NUM_MESSAGES; i++) {
    snprintf(msg, sizeof(msg), "Message #%d from producer (pid=%d)", i,
             getpid());

    int ret = nabd_push(q, msg, strlen(msg) + 1);

    if (ret == NABD_OK) {
      sent++;
      if (i < 5 || i >= NUM_MESSAGES - 5) {
        printf("[%03d] Sent: %s\n", i, msg);
      } else if (i == 5) {
        printf("...\n");
      }
    } else if (ret == NABD_FULL) {
      full_count++;
      usleep(1000); /* Wait 1ms and retry */
      i--;          /* Retry same message */
    } else {
      printf("[%03d] Error: %s\n", i, nabd_strerror(ret));
    }
  }

  printf("\nProducer finished:\n");
  printf("  Messages sent: %d\n", sent);
  printf("  Full events:   %d\n", full_count);

  /* Get stats */
  nabd_stats_t stats;
  if (nabd_stats(q, &stats) == NABD_OK) {
    printf("\nQueue stats:\n");
    printf("  Head: %llu\n", (unsigned long long)stats.head);
    printf("  Tail: %llu\n", (unsigned long long)stats.tail);
    printf("  Used: %llu/%llu\n", (unsigned long long)stats.used,
           (unsigned long long)stats.capacity);
  }

  /* Keep running to let consumer read */
  printf("\nProducer sleeping for 5 seconds (start consumer now)...\n");
  sleep(5);

  /* Cleanup */
  nabd_close(q);
  nabd_unlink(QUEUE_NAME);
  printf("Queue cleaned up.\n");

  return 0;
}
