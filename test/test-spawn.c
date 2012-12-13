#include <stdio.h>
#include <stdlib.h>
#include "gevent.h"
#include "task2.h"

static int count;
static gevent_cothread first, second;


static void noop(gevent_cothread* t) {
    ASSERT(t->state == GEVENT_COTHREAD_CURRENT);
    /*ASSERT(!t->stacklet);*/
    ASSERT(t->hub->current == t);
    ASSERT(!ngx_queue_empty(&t->hub->ready));
    ASSERT(ngx_queue_data(ngx_queue_head(&t->hub->ready), gevent_cothread, op) == (void*)&t->hub->main);
    count += 1;
}


/* spawn a no-op function */
TEST_IMPL(spawn_noop) {
    int i;
    gevent_cothread t;
    gevent_hub* hub = gevent_default_hub();
    for (i=1; i < 10; ++i) {
        ASSERT_just_main(hub);
        gevent_cothread_init(hub, &t, noop);
        SUCCESS(gevent_cothread_spawn(&t));
        ASSERT(count == i);
        ASSERT(t.state == GEVENT_COTHREAD_DEAD);
        /*ASSERT(!t.stacklet);*/
        ASSERT_just_main(hub);
    }
    return 0;
}


static void noop2(gevent_cothread* t) {
    ASSERT(t == &second);
    ASSERT(t->state == GEVENT_COTHREAD_CURRENT);
    /*ASSERT(!t->stacklet);*/
    ASSERT(t->hub->current == t);
    ASSERT(first.stacklet);
    ASSERT(first.stacklet != EMPTY_STACKLET_HANDLE);
    ASSERT(first.state == GEVENT_COTHREAD_READY);
    ASSERT(count == 1);
    count += 10;
}


static void spawn_noop(gevent_cothread* t) {
    ASSERT(t == &first);
    ASSERT(t->state == GEVENT_COTHREAD_CURRENT);
    /*ASSERT(!t->stacklet);*/
    ASSERT(t->hub->current == t);
    ASSERT(count == 0);
    count += 1;
    gevent_cothread_init(t->hub, &second, noop2);
    SUCCESS(gevent_cothread_spawn(&second));
    ASSERT(count == 111);
    count += 1000;
    ASSERT(second.state == GEVENT_COTHREAD_DEAD);
    /*ASSERT(!second.stacklet);*/
}


/* spawn a function that spawns another function that spawn a no-op */
TEST_IMPL(spawn_spawn_noop) {
    gevent_hub* hub = gevent_default_hub();
    ASSERT_just_main(hub);
    gevent_cothread_init(hub, &first, spawn_noop);
    SUCCESS(gevent_cothread_spawn(&first));
    ASSERT(count == 11);
    count += 100;
    SUCCESS(gevent_sleep(hub, 0));
    ASSERT(count == 1111);
    ASSERT(first.state == GEVENT_COTHREAD_DEAD);
    ASSERT(second.state == GEVENT_COTHREAD_DEAD);
    /*ASSERT(!first.stacklet);*/
    /*ASSERT(!second.stacklet);*/
    ASSERT_just_main(hub);
    return 0;
}


static void sleep0(gevent_cothread* t) {
    count += 1;
    SUCCESS(gevent_sleep(t->hub, 0));
    count += 10;
}


static void sleep1(gevent_cothread* t) {
    count += 1;
    SUCCESS(gevent_sleep(t->hub, 1));
    count += 10;
}


/* spawn cothread that sleeps */
TEST_IMPL(spawn_sleep) {
    count = 0;
    gevent_cothread t1, t2;
    gevent_hub* hub = gevent_default_hub();

    gevent_cothread_init(hub, &t1, sleep0);
    SUCCESS(gevent_cothread_spawn(&t1));
    ASSERT(t1.state == GEVENT_WAITING_TIMER);
    ASSERT(count == 1);
    SUCCESS(gevent_sleep(hub, 1));
    ASSERT(t1.state == GEVENT_COTHREAD_DEAD);
    ASSERT(count == 11);
    return 0;
}

static int spawned, slept, freed;


static void free_cothread(gevent_cothread* t) {
    ASSERT(t);
    free(t);
    freed++;
}


static void spawn_or_sleep(gevent_cothread* t) {
    gevent_cothread* new;
    if (rand() > RAND_MAX / 2) {
        spawned++;
        new = malloc(sizeof(gevent_cothread));
        gevent_cothread_init(t->hub, new, spawn_or_sleep);
        new->exit_fn = free_cothread;
        gevent_cothread_spawn(new);
    }
    else {
        slept++;
        gevent_sleep(t->hub, 1);
    }
}


TEST_IMPL(spawn_sleep_many) {
    gevent_hub* hub = gevent_default_hub();
    while (spawned < 100 || slept < 100) {
        spawn_or_sleep(hub->current);
    }
    gevent_wait(hub, 0);
    fprintf(stderr, "spawned=%d slept=%d freed=%d\n", spawned, slept, freed);
    ASSERT(spawned == freed);
    return 0;
}
