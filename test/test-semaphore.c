#include <stdio.h>
#include "gevent.h"
#include "task.h"


#define ZERO(x) ASSERT(((x) == 0) && #x);

static int count;
static gevent_semaphore sem;

static void acquirer(gevent_cothread* t) {
    ++count;
    int result = gevent_semaphore_acquire(&sem);
    ASSERT(result == 0);
    count += 100;
}

TEST_IMPL(semaphore) {
    gevent_hub* hub = gevent_default_hub();
    gevent_cothread t1, t2;
    gevent_channel_init(hub, &sem);
    gevent_cothread_init(hub, &t1, acquirer);
    gevent_cothread_init(hub, &t2, acquirer);

    gevent_cothread_spawn(&t1);
    ASSERT(count == 1);

    gevent_cothread_spawn(&t2);
    ASSERT(count == 2);

    ASSERT(t1.state == GEVENT_COTHREAD_CHANNEL_R);
    ASSERT(t2.state == GEVENT_COTHREAD_CHANNEL_R);

    gevent_semaphore_release(&sem);
    ASSERT(count == 102);

    ASSERT(t1.state == GEVENT_COTHREAD_DEAD);
    ASSERT(t2.state != GEVENT_COTHREAD_DEAD);

    gevent_semaphore_release(&sem);
    ASSERT(count == 202);

    ASSERT(t1.state == GEVENT_COTHREAD_DEAD);
    ASSERT(t2.state == GEVENT_COTHREAD_DEAD);
    return 0;
}
