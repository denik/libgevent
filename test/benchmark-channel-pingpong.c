#include "task2.h"
#include "gevent.h"


#define MAX_COUNT (1000 * 1000)


static int count;
gevent_channel channel;


static void pingpong(gevent_cothread* t) {
    void* result;
    while (count < MAX_COUNT) {
        SUCCESS(gevent_channel_receive(&channel, &result));
        ASSERT((int)result == count);
        ++count;
        SUCCESS(gevent_channel_send(&channel, (void*)count));
    }
}


BENCHMARK_IMPL(channel_pingpong) {
    uint64_t before;
    uint64_t after;
    gevent_cothread p;
    gevent_channel_init(gevent_default_hub(), &channel);
    gevent_cothread_init(gevent_default_hub(), &p, pingpong);
    before = uv_hrtime();
    gevent_cothread_spawn(&p);
    SUCCESS(gevent_channel_send(&channel, (void*)count));
    pingpong(NULL);
    after = uv_hrtime();
    LOGF("%.2f seconds to send+receive through channel %d times\n", (after - before) / 1e9, count);
    return 0;
}
