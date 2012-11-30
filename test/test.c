#include <stdio.h>
#include <assert.h>
#include "gevent.h"


void func1(gevent_cothread* t) {
    fprintf(stderr, "func1\n");
}

void func2(gevent_cothread* t) {
    fprintf(stderr, "func2\n");
    gevent_sleep(t->hub, 1000);
    fprintf(stderr, "func2 after 1s\n");
}

int main() {
    gevent_hub* hub = gevent_default_hub();
    gevent_cothread t1, t2;
    gevent_spawn(hub, &t1, func1);
    gevent_spawn(hub, &t2, func2);
    assert(gevent_wait(hub, 0) == 0);
    return 0;
}
