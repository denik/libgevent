#include "task.h"
#include "gevent.h"


#define LOG_SIZE(name) LOGF(#name ": %u bytes\n", (unsigned int) sizeof(name))


BENCHMARK_IMPL(sizes) {
    LOG_SIZE(gevent_cothread);
    LOG_SIZE(gevent_channel);
    LOG_SIZE(gevent_hub);
  return 0;
}
