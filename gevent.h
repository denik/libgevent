#ifndef _GEVENT_H_
#define _GEVENT_H_

#include "stacklet/stacklet.h"
#include "uv.h"
#include "libuv/include/uv-private/ngx-queue.h"


struct gevent_cothread_s;
struct gevent_hub_s;
typedef struct gevent_cothread_s gevent_cothread;
typedef struct gevent_hub_s gevent_hub;
typedef struct gevent_channel_s gevent_channel;
typedef void (*gevent_cothread_fn)(gevent_cothread*);


typedef enum gevent_cothread_state_e {
    GEVENT_COTHREAD_CURRENT = 0,
    GEVENT_COTHREAD_NEW = 1,
    GEVENT_COTHREAD_DEAD = 4,
#define XX(uc, lc) GEVENT_WAITING_##uc,
    UV_HANDLE_TYPE_MAP(XX)
    UV_REQ_TYPE_MAP(XX)
#undef XX
    GEVENT_CHANNEL_RECV,
    GEVENT_CHANNEL_SEND
} gevent_cothread_state;


#define GEVENT_ERRNO_MAP(XX)                                         \
  XX( 1000, EINTERRUPTED, "operation interrupted")                   \
  XX( 1001, ENOLOOP, "loop exited")                                  \
  XX( 1002, ENOMEM, "not enough memory")


#define GEVENT_ERRNO_GEN(val, name, s) GEVENT_##name = val,
typedef enum gevent_err_code_e {
  GEVENT_ERRNO_MAP(GEVENT_ERRNO_GEN)
  GEVENT_MAX_ERRORS
} gevent_err_code;
#undef GEVENT_ERRNO_GEN


struct gevent_cothread_s {
    gevent_hub* hub;
    stacklet_handle stacklet;

    /* storage for one-time operations, like sleep */
    union {
        gevent_cothread_fn run;

        /* not sure if I need all of them here;
         * for some perhaps pointer is better
         * */

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

        uv_write_t write;
        uv_connect_t connect;
        uv_shutdown_t shutdown;
        uv_fs_t fs_req;
        uv_work_t work_req;
        uv_udp_send_t udp_send_req;

        struct getaddrinfo_s {
            uv_getaddrinfo_t req;
            struct addrinfo** result;
        } getaddrinfo;

        struct channel_s {
            ngx_queue_t queue;
            void* value;
        } channel;

        /* could also be
         * - pointer to external handle or req  (if we need this???)
         * - pointer to event/queue/semaphore that we wait for
         * */
    } op;
    int state; /* could be int16?*/
    int op_status;
};


struct gevent_hub_s {
    uv_loop_t* loop;
    stacklet_thread_handle thread;
    stacklet_handle stacklet;
    gevent_cothread* current;
    gevent_cothread main;
    gevent_cothread_fn exit_fn;
};


struct gevent_channel_s {
    ngx_queue_t receivers;
    ngx_queue_t senders;
    gevent_hub* hub;
};

int gevent_hub_init(gevent_hub* hub, uv_loop_t* loop);
gevent_hub* gevent_default_hub();

void gevent_cothread_init(gevent_hub* hub, gevent_cothread* t, gevent_cothread_fn run);
void gevent_spawn(gevent_hub* hub, gevent_cothread* t, gevent_cothread_fn run);
int gevent_yield(gevent_hub* hub);
void gevent_activate(gevent_cothread* t);

int gevent_sleep(gevent_hub* hub, int64_t timeout);
int gevent_wait(gevent_hub* hub, int64_t timeout);

int gevent_channel_init(gevent_hub* hub, gevent_channel* ch);
int gevent_channel_receive(gevent_channel* ch, void** result);
int gevent_channel_send(gevent_channel* ch, void* value);

/*
 * Most functions return boolean: 0 for success and -1 for failure.
 * On error the user should then call gevent_last_error() to determine
 * the error code.
 *
 * libgevent adds a few more error codes on top of libuv's error codes
 * use these functions to make sure you can work with them
 */
//UV_EXTERN uv_err_t gevent_last_error(gevent_hub*);
UV_EXTERN const char* gevent_strerror(int code);
UV_EXTERN const char* gevent_err_name(int code);

/*
 * Synchronous getaddrinfo(3).
 *
 * Either node or service may be NULL but not both.
 *
 * hints is a pointer to a struct addrinfo with additional address type
 * constraints, or NULL. Consult `man -s 3 getaddrinfo` for details.
 *
 * Returns 0 on success, -1 on error. Call gevent_last_error() to get the error.
 *
 * If successful, it returns 0 and *res is updated to point to a valid struct addrinfo
 *
 * On NXDOMAIN, the status code is -1 and gevent_last_error() returns UV_ENOENT.
 *
 * Call uv_freeaddrinfo() to free the addrinfo structure.
 */

// XXX why don't we just return struct addrinfo* ?
// XXX if it's NULL, check for error

int gevent_getaddrinfo(gevent_hub* hub,
                       const char *node,
                       const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res);


#endif
