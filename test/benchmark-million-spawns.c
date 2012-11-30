#include "task2.h"
#include "gevent.h"

#define NUM_COTHREADS (1000 * 1000)

static int count;
static gevent_hub* hub;


static void noop(gevent_cothread* t) {
    count++;
}


static void sleep0(gevent_cothread* t) {
    gevent_sleep(hub, 0);
    count++;
}


int do_bench(gevent_cothread_fn func, char* explanation) {
  gevent_cothread* cothreads;
  uint64_t before;
  uint64_t after;
  int i;

  cothreads = malloc(NUM_COTHREADS * sizeof(cothreads[0]));
  ASSERT(cothreads);

  hub = gevent_default_hub();
  count = 0;

  for (i = 0; i < NUM_COTHREADS; i++) {
      gevent_cothread_init(hub, &cothreads[i], func);
  }

  before = uv_hrtime();
  for (i = 0; i < NUM_COTHREADS; i++) {
      /* gevent_cothread_init(hub, &cothreads[i], func); */
      SUCCESS(gevent_cothread_spawn(&cothreads[i]));
  }
  SUCCESS(gevent_wait(hub, 0));
  after = uv_hrtime();

  ASSERT(count == NUM_COTHREADS);
  for (i = 0; i < NUM_COTHREADS; i++) {
      ASSERT(cothreads[i].state == GEVENT_COTHREAD_DEAD);
  }
  free(cothreads);

  LOGF("%.2f seconds %s\n", (after - before) / 1e9, explanation);

  /* MAKE_VALGRIND_HAPPY(); */
  return 0;
}


BENCHMARK_IMPL(million_spawns_noop) {
  return do_bench(noop, "to spawn a million of no-op functions");
}


BENCHMARK_IMPL(million_spawns_sleep0) {
  return do_bench(sleep0, "to spawn a million of sleep(0) functions");
}
