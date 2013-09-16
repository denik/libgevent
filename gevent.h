#ifndef _GEVENT_H_
#define _GEVENT_H_

#include "stacklet/stacklet.h"
#include "uv.h"
#include "uv-private/ngx-queue.h"


/* Return values:
 *
 * All functions returning int return -1 on error and 0 on success.
 */

/* Switching:
 *
 * Most functions that can switch immediatelly to the target do switch immediatelly rather
 * then doing it via hub (spawn, channel_recv, channel_send).
 *
 * TODO:
 * However, to avoid starving the event loop, a counter is maintained of how many switches were
 * done that bypassed the hub. When this count reaches certain value (GEVENT_SWITCH_COUNT,
 * default 100) a switch to the hub is forced.
 */


struct gevent_cothread_s;
struct gevent_hub_s;
typedef struct gevent_cothread_s gevent_cothread;
typedef struct gevent_hub_s gevent_hub;
typedef struct gevent_channel_s gevent_channel;
typedef void (*gevent_cothread_fn)(gevent_cothread*);


typedef enum gevent_cothread_state_e {
    /* the cothread is currently active */
    GEVENT_COTHREAD_CURRENT = 0,

    /* the cothread has never been switched to (op.init.run points to function) */
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

#define XX(uc, lc) GEVENT_WAITING_PTR_##uc,
    UV_HANDLE_TYPE_MAP(XX)
    UV_REQ_TYPE_MAP(XX)
#undef XX

    GEVENT_COTHREAD_LAST
} gevent_cothread_state;


#define GEVENT_COTHREAD_IS_DEAD(t) ((t)->state == GEVENT_COTHREAD_DEAD)


struct gevent_cothread_s {
    /* all of the members below are read-only */
    gevent_hub* hub;

    gevent_cothread_state state;

    /* storage for one-time operations */
    union {
        /* GEVENT_COTHREAD_NEW */
        struct gevent_cothread_init_s {
            gevent_cothread_fn run;
            void* args[6];
        } init;

        /* GEVENT_COTHREAD_READY */
        ngx_queue_t ready;

        /* GEVENT_WAITING_TIMER */
        uv_timer_t timer;

        /* GEVENT_WAITING_GETADDRINFO */
        struct gevent_cothread_getaddrinfo_s {
            uv_getaddrinfo_t* req;
            struct addrinfo* res;
            int error;
        } getaddrinfo;

        /* GEVENT_COTHREAD_CHANNEL_[RS] */
        struct gevent_cothread_channel_s {
            ngx_queue_t queue;
            void* value;
        } channel;
    } op;

    /* private: stored stack */
    stacklet_handle stacklet;

    /* this will be called when a cothread has finished and will not be used by gevent anymore
     * this must not be null; the default value is  */
    gevent_cothread_fn exit_fn;

    /* this is called before cothread is about to switch to another cothread */
    gevent_cothread_fn switch_out;

    /* this is called after cothread has been switched to */
    gevent_cothread_fn switch_in;


    /* TODO:
     * - background/daemon flag: all handles/requests are temporarily unrefed before waiting for them
     */
};


struct gevent_hub_s {
    /* libuv event loop attached to this hub */
    uv_loop_t* loop;

    /* initially 1; set to zero after uv_run() exits;
     * QQQ wouldn't need this if libuv exposed uv__loop_alive(); */
    int loop_alive;

    /* a list of ready-to-run cothreads */
    ngx_queue_t ready;

    /* the stacklet-related bookkeeping about the thread this hub is attached to */
    stacklet_thread_handle thread;

    /* stacklet to resume the hub; if hub->current is NULL then it is garbage */
    stacklet_handle stacklet;

    /* read-only: main cothread represents the initial thread of execution; (it needs no spawning) */
    gevent_cothread main;

    /* pointer to the cothread that is currently active; NULL if hub is currently active; initially it is &main */
    gevent_cothread* current;
};


struct gevent_channel_s {
    ngx_queue_t receivers;
    ngx_queue_t senders;
    gevent_hub* hub;
};

/* Returns the default hub (initialized with the default loop). */
gevent_hub* gevent_default_hub(void);


/* Initialize a non-default hub.
 *
 * You don't need to use it for the default hub.
 */
int gevent_hub_init(gevent_hub* hub, uv_loop_t* loop);


/* Initialize the cothread structure. */
void gevent_cothread_init(gevent_hub* hub, gevent_cothread* t, gevent_cothread_fn run);

/* Start a cothread.
 *
 * Switch into it immediatelly (the current one is put in a READY queue).
 */
void gevent_cothread_spawn(gevent_cothread* t);

/* Pause the current cothread for a specified number of miliseconds */
int gevent_sleep(gevent_hub* hub, int64_t timeout);

/* Wait for the event loop to finish or for a specified number of miliseconds, whatever happens first. */
int gevent_wait(gevent_hub* hub, int64_t timeout);

/* Initialize a channel structure */
void gevent_channel_init(gevent_hub* hub, gevent_channel* ch);

/* Send a value through a channel.
 *
 * If there's one or more receivers blocking on this channel, the
 * first receiver in the queue is woken up and passed the value.
 * The current cothread is added to READY queue in such case.
 *
 * If there there no receievers at the moment, the current cothread is paused.
 */
void gevent_channel_send(gevent_channel* ch, void* value);

/* Receive a value through a channel.
 *
 * If there's one or more senders blocking on this channel, the first one
 * in the queue is unlocked, while the current cothread is added to the READY queue.
 * The value of the sender is passed to *result.
 *
 * If there are no senders at the moment, the current cothread is paused.
 */
void gevent_channel_receive(gevent_channel* ch, void** result);

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

#endif
