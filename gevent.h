#ifndef _GEVENT_H_
#define _GEVENT_H_

#include "stacklet/stacklet.c"
#include "uv.h"
#include "libuv/include/uv-private/ngx-queue.h"

struct gevent_cothread_s;
struct gevent_hub_s;
typedef struct gevent_cothread_s gevent_cothread;
typedef struct gevent_hub_s gevent_hub;
//typedef struct gevent_cothread_s* gevent_cothread_handle;
//typedef struct gevent_hub_s* gevent_hub_handle;
typedef void (*gevent_run_fn)(gevent_cothread*);


#define GEVENT_FLAG_MASK        0x0ffff
#define GEVENT_FLAG_RUN         0x10000
#define GEVENT_FLAG_HANDLE      0x20000
#define GEVENT_FLAG_REQ         0x40000


struct gevent_cothread_s {
    ngx_queue_t spawned; // XXX rename to active
    gevent_hub* hub;
    stacklet_handle stacklet;

    /* 0 means cothread is currently active; op is garbage
     * GEVENT_FLAG_RUN means cothread was created but never switched to yet (op->run)
     * GEVENT_FLAG_HANDLE bit set means (flags & GEVENT_FLAG_MASK) is one of UV handles (op->stream,...)
     * GEVENT_FLAG_REQ bit set means (flag & GEVENT_FLAG_REQ) is one of UV requests (op->write,...)
     * */
    int flags;

    /* storage for one-time operations, like sleep */
    union {
        /* flags | GEVENT_FLAG_RUN */
        gevent_run_fn run;

        /* flags | GEVENT_FLAG_HANDLE */
        uv_stream_t stream;
        uv_tcp_t tcp;
        uv_pipe_t pipe;
        uv_prepare_t prepare;
        uv_check_t check;
        uv_idle_t idle;
        uv_async_t async;
        uv_timer_t timer;
        uv_fs_event_t fs_event;
        uv_fs_poll_t fs_poll;
        uv_poll_t poll;
        uv_process_t process;
        uv_tty_t tty;
        uv_udp_t udp;

        /* flags | GEVENT_FLAG_REQ */
        uv_write_t write;
        uv_connect_t connect;
        uv_shutdown_t shutdown;
        uv_fs_t fs_req;
        uv_work_t work_req;
        uv_udp_send_t udp_send_req;
        uv_getaddrinfo_t getaddrinfo_req;

        /* could also be
         * - pointer to external handle or req  (if we need this???)
         * - pointer to event/queue/semaphore that we wait for
         * */
    } op;
};

struct gevent_hub_s {
    ngx_queue_t spawned;
    uv_loop_t* loop;
    stacklet_thread_handle thread;
    stacklet_handle stacklet;
    gevent_cothread* current;
    gevent_cothread main;
    struct uv_prepare_s prepare;
};

void gevent_cothread_init(gevent_hub* hub, gevent_cothread* t, gevent_run_fn run);
void gevent_hub_init(gevent_hub* hub, uv_loop_t* loop);
gevent_hub* gevent_default_hub();
int pause_current(gevent_hub* hub);

void gevent_activate(gevent_cothread* t);
void gevent_spawn(gevent_hub* hub, gevent_cothread* t, gevent_run_fn run);
 
int gevent_sleep(gevent_hub* hub, int64_t timeout);
int gevent_wait(gevent_hub* hub);

#endif
