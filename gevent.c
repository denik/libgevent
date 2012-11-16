#include "gevent.h"
#include <stdio.h>

static gevent_hub _default_hub;
static gevent_hub* default_hub_handle = NULL;

// XXX call it cothread because that's what it is

void gevent_cothread_init(gevent_hub* hub, gevent_cothread* t, gevent_run_fn run) {
    ngx_queue_init(&t->spawned);
    t->spawned.next = NULL;
    t->hub = hub;
    t->op.run = run;
    t->flags = GEVENT_FLAG_RUN;
    t->stacklet = NULL;
}

void gevent_hub_init(gevent_hub* hub, uv_loop_t* loop)
{
    ngx_queue_init(&hub->spawned);
    hub->loop = loop;
    hub->thread = stacklet_newthread();
    assert(hub->thread);
    // XXX return error to the caller (so that Python wrapper can raise MemoryError)
    hub->stacklet = NULL;
    hub->current = &hub->main;
    gevent_cothread_init(hub, &hub->main, NULL);
    uv_prepare_init(hub->loop, &hub->prepare);
}

gevent_hub* gevent_default_hub() {
    if (!default_hub_handle) {
        default_hub_handle = &_default_hub;
        gevent_hub_init(default_hub_handle, uv_default_loop());
    }
    return default_hub_handle;
}

stacklet_handle hub_run(stacklet_handle main, void* data) {
    gevent_hub* hub = (gevent_hub*)data;
    hub->main.stacklet = main;
    uv_run(hub->loop);
    // set switching reason to LOOP_DONE (so that Python can raise LoopExited)
    return hub->main.stacklet;
}

// returns 0 on success and hub is still active
// returns 1 on success and the loop has finished
// return -1 on error (stacklet failed to allocate)
int pause_current(gevent_hub* hub) { 
    stacklet_handle source;
    assert(hub->thread);
    if (hub->stacklet) {
        source = stacklet_switch(hub->thread, hub->stacklet);
    }
    else {
        source = stacklet_new(hub->thread, hub_run, hub);
    }
    // source is always the new stacklet for hub or EMPTY_STACKLET_HANDLE if uv_loop is done
    if (source == EMPTY_STACKLET_HANDLE) {
        hub->stacklet = NULL;
        return 1;
    }
    hub->stacklet = source;
    return source ? 0 : -1;
    // XXX use libuv's last error facility
}

stacklet_handle wrapper_fn(stacklet_handle parent, gevent_cothread* g) {
    gevent_run_fn run = g->op.run;
    assert(run);
    assert(g->flags == GEVENT_FLAG_RUN);
    g->op.run = NULL;
    g->flags = 0;
    g->hub->stacklet = parent;
    run(g);
    return g->hub->stacklet;
}

void gevent_cothread_switch(gevent_cothread* g) {
    stacklet_handle source;
    gevent_hub* hub = g->hub;
    assert(hub->thread);
    if (g->spawned.next) {
        ngx_queue_remove(&g->spawned);
    }
    hub->current = g;
    if (g->flags & GEVENT_FLAG_RUN) {
        assert(g->flags == GEVENT_FLAG_RUN);
        assert(g->op.run);
        source = stacklet_new(hub->thread, (stacklet_run_fn)wrapper_fn, g);
    }
    else {
        source = stacklet_switch(hub->thread, g->stacklet);
    }
    // source is the stacklet that just switched to the hub
    hub->current->stacklet = source;
}

void prepare_callback(uv_prepare_t* handle, int status) {
    gevent_hub* hub = ngx_queue_data(handle, gevent_hub, prepare);
    while (!ngx_queue_empty(&hub->spawned)) {
        gevent_cothread* pt = ngx_queue_data(ngx_queue_head(&hub->spawned), gevent_cothread, spawned);
        gevent_cothread_switch(pt);
    }
    uv_prepare_stop(&hub->prepare);
}

void activate(gevent_cothread* t) {
    if (!ngx_queue_empty(&t->spawned)) return;
    ngx_queue_insert_tail(&t->hub->spawned, &t->spawned);
    uv_prepare_start(&t->hub->prepare, prepare_callback);
}

void gevent_spawn(gevent_hub* hub, gevent_cothread* t, gevent_run_fn run) {
    gevent_cothread_init(hub, t, run);
    activate(t);
}
 
void switch_cb(uv_timer_t* handle, int status) {
    // QQQ pass status along; check for which watchers it actually matters
    gevent_cothread* paused = (gevent_cothread*)handle->data;
    gevent_cothread_switch(paused);
}

int gevent_sleep(gevent_hub* hub, int64_t timeout) {
    int retcode;
    gevent_cothread* current;
    assert(hub);
    current = hub->current;
    assert(current);
    assert(current->flags == 0);
    uv_timer_t* ponce = &hub->current->op.timer;
    uv_timer_init(hub->loop, ponce);  // XXX retcode
    current->flags = GEVENT_FLAG_HANDLE | UV_TIMER;
    ponce->data = current;
    uv_timer_start(ponce, switch_cb, timeout, 0);  // XXX retcode
    retcode = pause_current(hub);
    // XXX check that switch is good; otherwise set error to STUV_INTERRUPTED and return -1
    current->flags = 0;
    assert(retcode == 0);
    // XXX retcode can actually be -1, which is sad but normal (MemoryError)
    uv_timer_stop(ponce);
    return 0;
    // XXX in case of error, make sure we
    // 1) stopped the timer
    // 2) reset once_type to something neutral
    // 3) set uv_last_error to something we support
}

int gevent_wait(gevent_hub* hub) {
    int retcode = pause_current(hub);
    assert(retcode == 1);
}
/*

typedef void (*uv_getaddrinfo_cb)(uv_getaddrinfo_t* req,
                                  int status,
                                  struct addrinfo* res);

static void getaddrinfo_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
}


int gevent_getaddrinfo(gevent_hub* hub,
                       const char *node,
                       const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res)
{
    int retcode;
    gevent_cothread* current;
    assert(hub);
    current = hub->current;
    assert(current);
    assert(current->once_type == -1);
    uv_timer_t* ponce = &hub->current->once.timer;
    retcode = uv_getaddrinfo(hub->loop,
                             uv_getaddrinfo_t* req,
                             uv_getaddrinfo_cb getaddrinfo_cb,
                             const char* node,
                             const char* service,
                             const struct addrinfo* hints);




}
*/

/*
 * Synchronous getaddrinfo(3).
 *
 * Either node or service may be NULL but not both.
 *
 * hints is a pointer to a struct addrinfo with additional address type
 * constraints, or NULL. Consult `man -s 3 getaddrinfo` for details.
 *
 * Returns 0 on success, -1 on error. Call uv_last_error() to get the error.
 *
 * If successful, your callback gets called sometime in the future with the
 * lookup result, which is either:
 *
 *  a) status == 0, the res argument points to a valid struct addrinfo, or
 *  b) status == -1, the res argument is NULL.
 *
 * On NXDOMAIN, the status code is -1 and uv_last_error() returns UV_ENOENT.
 *
 * Call uv_freeaddrinfo() to free the addrinfo structure.
 */
/*
UV_EXTERN int uv_getaddrinfo(uv_loop_t* loop,
                             uv_getaddrinfo_t* req,
                             uv_getaddrinfo_cb getaddrinfo_cb,
                             const char* node,
                             const char* service,
                             const struct addrinfo* hints);

*/
