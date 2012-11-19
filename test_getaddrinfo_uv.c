#include <stdio.h>
#include "gevent.h"


uv_loop_t* loop;


static void on_resolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    if (status) {
        fprintf(stderr, "getaddrinfo call error %s\n", uv_err_name(uv_last_error(loop)));
    } else {
        fprintf(stderr, "%s\n", res->ai_canonname);
    }
}


int main() {
    loop = uv_default_loop();

    struct addrinfo hints;
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    uv_getaddrinfo_t resolver;
    //fprintf(stderr, "irc.freenode.net is... ");
    fprintf(stderr, "localhost is... ");
    int r = uv_getaddrinfo(loop, &resolver, on_resolved, "localhost", "6667", &hints);

    if (r) {
        fprintf(stderr, "getaddrinfo call error %s\n", uv_err_name(uv_last_error(loop)));
        return 1;
    }
    return uv_run(loop);
}
