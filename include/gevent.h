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
void gevent_noop(gevent_cothread* t);


typedef enum gevent_cothread_state_e {
    /* the cothread is currently active */
    GEVENT_COTHREAD_CURRENT = 0,

    /* the cothread has never been switched to (op.run points to function) */
    GEVENT_COTHREAD_NEW = 1,

    /* the cothread is ready to run (referenced by ready queue) */
    GEVENT_COTHREAD_READY = 2,

    /* the cothread has finished */
    GEVENT_COTHREAD_DEAD = 4,

    /* the cothread is blocking on channel receive/send (op.channel) */
    GEVENT_COTHREAD_CHANNEL_R = 5,
    GEVENT_COTHREAD_CHANNEL_S = 6,

    GEVENT_COTHREAD__SEP = 32,

#define XX(uc, lc) GEVENT_WAITING_##uc,
    UV_HANDLE_TYPE_MAP(XX)
    UV_REQ_TYPE_MAP(XX)
#undef XX

    GEVENT_COTHREAD_LAST
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


#define GEVENT_COTHREAD_IS_DEAD(t) ((t)->state == GEVENT_COTHREAD_DEAD)


struct gevent_cothread_s {
    /* all of the members below are private and read-only */

    ngx_queue_t ready;

    gevent_hub* hub;

    gevent_cothread_state state;

    void* current_op;

    stacklet_handle stacklet;

    /* storage for one-time operations, like sleep */
    union {
        gevent_cothread_fn run;
        uv_timer_t timer;

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
    int op_status; /* not everyone needs it */

    /* likely features:
     * - background flag: if set all handles/requests are temporarily unrefed before waiting for them
     */
};


struct gevent_hub_s {
    /* everything in hub structure is read-only, except for exit_fn, which can be set at any time */

    /* a list of ready-to-run cothreads */
    ngx_queue_t ready;

    /* libuv event loop attached to this hub */
    uv_loop_t* loop;

    /* the stacklet-related bookkeeping about the thread this hub is attached to */
    stacklet_thread_handle thread;

    /* stacklet to resume the hub; if hub->current is NULL then it is garbage */
    stacklet_handle stacklet;

    /* read-only: main cothread represents the initial thread of execution; (it needs no spawning) */
    gevent_cothread main;

    /* pointer to the cothread that is currently active; NULL if hub is currently active; initially it is &main */
    gevent_cothread* current;

    /* this will be called when a cothread has finished and will not be used by gevent anymore
     * this must not be null; the default value is  */
    gevent_cothread_fn exit_fn;
};


struct gevent_channel_s {
    ngx_queue_t receivers;
    ngx_queue_t senders;
    gevent_hub* hub;
};

/* Return values:
 *
 * Most functions returning int return -1 on error and 0 on success (like libuv)
 * The exception is gevent_wait() which can also return 1.
 */

/* Switching:
 * Most functions that can switch immediatelly to the target do switch immediatelly rather
 * then doing it via hub (spawn, channel_recv, channel_send).
 *
 * However, to avoid starving hub, this function maintain a count of switches that bypassed the hub.
 * When this count reaches certain value (GEVENT_SWITCH_COUNT) the hub is get switched to.
 * When it happens, a zero timer is started to ensure the event loop will not go to sleep.
 */

#define GEVENT_SWITCH_COUNT 100


int gevent_hub_init(gevent_hub* hub, uv_loop_t* loop);
gevent_hub* gevent_default_hub(void);

/* Initialize a cothread structure */
void gevent_cothread_init(gevent_hub* hub, gevent_cothread* t, gevent_cothread_fn run);

/* Start a cothread: switch into it and add the current cothread to 'ready' queue */
int gevent_cothread_spawn(gevent_cothread* t);

/* Pause a cothread for a specified number of miliseconds */
int gevent_sleep(gevent_hub* hub, int64_t timeout);

/* Wait for the event loop to finish or for a specified number of miliseconds, whatever happens first.
 * Returns -1 on error, 0 if timeout expired, 1 if event loop has finished.
 * */
int gevent_wait(gevent_hub* hub, int64_t timeout);

/* Initialize a channel structure */
int gevent_channel_init(gevent_hub* hub, gevent_channel* ch);

/* Send a value through a channel. Blocks until the value is actually consumed. */
int gevent_channel_send(gevent_channel* ch, void* value);

/* Receive a value through a channel. Blocks until
 * If there is one or more senders blocking on this channel, the oldest one is unlocked
 *
 */
int gevent_channel_receive(gevent_channel* ch, void** result);

/*
 * Most functions return boolean: 0 for success and -1 for failure.
 * On error the user should then call gevent_last_error() to determine
 * the error code.
 *
 * libgevent adds a few more error codes on top of libuv's error codes
 * use these functions to make sure you can work with them
 */
/* UV_EXTERN uv_err_t gevent_last_error(gevent_hub*); */
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

/* XXX why don't we just return struct addrinfo* ?
   XXX if it's NULL, check for error
*/
int gevent_getaddrinfo(gevent_hub* hub,
                       const char *node,
                       const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res);

/* low-level */
int gevent_yield(gevent_hub* hub);

#endif
