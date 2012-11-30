#include <stdio.h>
#include "gevent.h"


int main() {
#define PRINT_SIZEOF(type) fprintf(stderr, "sizeof " #type "= %u\n", sizeof(type));
    PRINT_SIZEOF(int);
    PRINT_SIZEOF(long);
    PRINT_SIZEOF(gevent_cothread);
    PRINT_SIZEOF(gevent_hub);

    fprintf(stderr, "\n");

    PRINT_SIZEOF(uv_stream_t);
    PRINT_SIZEOF(uv_tcp_t);
    PRINT_SIZEOF(uv_pipe_t);
    PRINT_SIZEOF(uv_prepare_t);
    PRINT_SIZEOF(uv_check_t);
    PRINT_SIZEOF(uv_idle_t);
    PRINT_SIZEOF(uv_async_t);
    PRINT_SIZEOF(uv_timer_t);
    PRINT_SIZEOF(uv_fs_event_t);
    PRINT_SIZEOF(uv_fs_poll_t);
    PRINT_SIZEOF(uv_poll_t);
    PRINT_SIZEOF(uv_process_t);
    PRINT_SIZEOF(uv_tty_t);
    PRINT_SIZEOF(uv_udp_t);

    fprintf(stderr, "\n");

    PRINT_SIZEOF(uv_write_t);
    PRINT_SIZEOF(uv_connect_t);
    PRINT_SIZEOF(uv_shutdown_t);
    PRINT_SIZEOF(uv_fs_t);
    PRINT_SIZEOF(uv_work_t);
    PRINT_SIZEOF(uv_udp_send_t);
    PRINT_SIZEOF(uv_getaddrinfo_t);

    return 0;
}


/*
        gevent_cothread_fn run;

        uv_write_t write;
        uv_connect_t connect;
        uv_shutdown_t shutdown;
        uv_fs_t fs_req;
        uv_work_t work_req;
        uv_udp_send_t udp_send_req;
        uv_getaddrinfo_t getaddrinfo_req;
*/

