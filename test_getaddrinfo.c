#include <stdio.h>
#include "gevent.h"



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
