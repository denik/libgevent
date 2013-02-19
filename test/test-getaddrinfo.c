#include <stdio.h>
#include "gevent.h"
#include "task2.h"


TEST_IMPL(getaddrinfo_basic) {
    struct addrinfo* res = NULL;
    gevent_hub* hub = gevent_default_hub();
    ASSERT_just_main(hub);
    SUCCESS(gevent_getaddrinfo(hub, "localhost", NULL, NULL, &res));
    ASSERT(res);
    uv_freeaddrinfo(res);
    ASSERT_just_main(hub);
    return 0;
}


static int count;


static void run_getaddrinfo(gevent_cothread* t) {
    struct addrinfo** res = malloc(sizeof(void*));
    ASSERT(res);
    gevent_hub* hub = gevent_default_hub();
    SUCCESS(gevent_getaddrinfo(hub, "localhost", NULL, NULL, res));
    uv_freeaddrinfo(*res);
    count += 1;
}


TEST_IMPL(getaddrinfo_concurrent) {
    int i;
    gevent_cothread t[100];
    struct addrinfo* res;
    gevent_hub* hub = gevent_default_hub();
    ASSERT_just_main(hub);
    for (i=0; i<100; ++i) {
        gevent_cothread_init(hub, &t[i], run_getaddrinfo);
        SUCCESS(gevent_cothread_spawn(&t[i]));
        ASSERT(t[i].state == GEVENT_WAITING_GETADDRINFO);
    }
    SUCCESS(gevent_wait(hub, 0));
    ASSERT_just_main(hub);
    return 0;
}
/*

int main() {
    gevent_hub* hub = gevent_default_hub();
    struct addrinfo* res;
    struct addrinfo hints;
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    //fprintf(stderr, "irc.freenode.net is... ");
    fprintf(stderr, "gevent localhost is... ");
    int r = gevent_getaddrinfo(hub, "gevent.org", "80", &hints, &res);

    if (r) {
        fprintf(stderr, "getaddrinfo call error %s\n", gevent_err_name(uv_last_error(hub->loop).code));
        return 1;
    }
    else {
        fprintf(stderr, "%s\n", res->ai_canonname);
        return 0;
    }
}
*/
