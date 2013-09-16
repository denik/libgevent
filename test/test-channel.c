#include <stdio.h>
#include "gevent.h"
#include "task.h"


#define ZERO(x) ASSERT(((x) == 0) && #x);

static int count;
static gevent_channel ch;

static void receiver(gevent_cothread* t) {
    void* data;
    ++count;
    gevent_channel_receive(&ch, &data);
    ASSERT(data);
    count += (int)data;
}

TEST_IMPL(channel) {
    gevent_hub* hub = gevent_default_hub();
    gevent_cothread t1, t2;
    gevent_channel_init(hub, &ch);
    gevent_cothread_init(hub, &t1, receiver);
    gevent_cothread_init(hub, &t2, receiver);

    gevent_cothread_spawn(&t1);
    ASSERT(count == 1);

    gevent_cothread_spawn(&t2);
    ASSERT(count == 2);

    ASSERT(t1.state == GEVENT_COTHREAD_CHANNEL_R);
    ASSERT(t2.state == GEVENT_COTHREAD_CHANNEL_R);

    gevent_channel_send(&ch, (void*)100);
    ASSERT(count == 102);

    ASSERT(t1.state == GEVENT_COTHREAD_DEAD);
    ASSERT(t2.state != GEVENT_COTHREAD_DEAD);

    gevent_channel_send(&ch, (void*)10000);
    ASSERT(count == 10102);

    ASSERT(t1.state == GEVENT_COTHREAD_DEAD);
    ASSERT(t2.state == GEVENT_COTHREAD_DEAD);
    return 0;
}
