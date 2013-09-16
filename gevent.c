#include "gevent.h"
#include "stacklet/stacklet.c"
#include <stdio.h>


#ifndef GEVENT_SWITCH_COUNT
#define GEVENT_SWITCH_COUNT 100
#endif


static gevent_hub default_hub_struct;
static gevent_hub* default_hub_ptr = NULL;
static void gevent_switch(gevent_cothread* t);


static uv_err_t new_artificial_error(uv_err_code code) {
  uv_err_t error;
  error.code = code;
  error.sys_errno_ = 0;
  return error;
}


static int set_artificial_error(uv_loop_t* loop, uv_err_code code) {
  loop->last_err = new_artificial_error(code);
  return -1;
}


int gevent_hub_init(gevent_hub* hub, uv_loop_t* loop)
{
    assert(hub && loop);
    hub->loop = loop;
    hub->loop_alive = 1;
    ngx_queue_init(&hub->ready);
    hub->thread = stacklet_newthread();
    if (!hub->thread) {
        return -1;
    }
    hub->stacklet = NULL;
    gevent_cothread_init(hub, &hub->main, NULL);
    hub->main.state = GEVENT_COTHREAD_CURRENT;
    hub->current = &hub->main;
    return 0;
}


gevent_hub* gevent_default_hub() {
    uv_loop_t* loop;

    if (default_hub_ptr)
        return default_hub_ptr;

    loop = uv_default_loop();
    if (!loop)
        return NULL;

    if (gevent_hub_init(&default_hub_struct, loop))
        return NULL;

    return (default_hub_ptr = &default_hub_struct);
}


void gevent_cothread_init(gevent_hub* hub, gevent_cothread* t, gevent_cothread_fn run) {
    t->hub = hub;
    t->state = GEVENT_COTHREAD_NEW;
    t->op.init.run = run;
    t->stacklet = NULL; /* not needed, debug only */
    t->exit_fn = NULL;
}


static gevent_cothread* get_current(gevent_hub* hub) {
    gevent_cothread* current;
    assert(hub);
    current = hub->current;
    assert(current);
    assert(current->state == GEVENT_COTHREAD_CURRENT);
    return current;
}


static void _activate(gevent_cothread* t) {
    assert(t->state == GEVENT_COTHREAD_CURRENT);
    t->state = GEVENT_COTHREAD_READY;
    ngx_queue_insert_tail(&t->hub->ready, &t->op.ready);
}


void gevent_cothread_spawn(gevent_cothread* t) {
    gevent_cothread* current = get_current(t->hub);
    if (current) {
        _activate(current);
    }
    assert(t->state == GEVENT_COTHREAD_NEW);
    gevent_switch(t);
}


void _before_switch(gevent_hub* hub, gevent_cothread* new) {
    assert(hub);
    /* fprintf(stderr, "_before_switch hub=%x from=%x to=%x\n", (unsigned)hub, (unsigned)hub->current, (unsigned)new); */
    assert(new || hub->current);
}


void _after_switch(gevent_hub* hub, gevent_cothread* new, stacklet_handle source) {
    gevent_cothread* who_died = NULL;
    /* fprintf(stderr, "_after_switch hub=%x from=%x to=%x stacklet=%x\n", (unsigned)hub, (unsigned)hub->current, (unsigned)new, (unsigned)source); */
    assert(source);
    if (hub->current) {
        /* we switched from another cothread */
        if (source == EMPTY_STACKLET_HANDLE) {
            assert(hub->current != &hub->main);
            assert(hub->current->state == GEVENT_COTHREAD_CURRENT);
            hub->current->state = GEVENT_COTHREAD_DEAD;
            who_died = hub->current;
            /* when cothread is DEAD, stacklet is garbage */
            hub->current->stacklet = NULL;
        }
        else {
            assert(source);
            hub->current->stacklet = source;
        }
    }
    else {
        /* we switched from the hub */
        if (source == EMPTY_STACKLET_HANDLE) {
            /* hub has finished */
            hub->stacklet = NULL;
        }
        else {
            assert(source);
            hub->stacklet = source;
        }
    }
    assert(hub->current != new);
    hub->current = new;
    if (new) {
        new->state = GEVENT_COTHREAD_CURRENT;
        /* for easier debugging */
        new->stacklet = NULL;
    }
    else {
        hub->stacklet = NULL;
    }
    if (who_died && who_died->exit_fn) {
        who_died->exit_fn(who_died);
    }
}


void _after_failed_switch(gevent_cothread* current) {
    if (current) {
        if (current->state == GEVENT_COTHREAD_READY) {
            ngx_queue_remove(&current->op.ready);
        }
        current->state = GEVENT_COTHREAD_CURRENT;
    }
}


stacklet_handle _get_next_stacklet(gevent_hub* hub) {
    ngx_queue_t* q;
    gevent_cothread* tmp;
    if (!ngx_queue_empty(&hub->ready)) {
        q = ngx_queue_head(&hub->ready);
        tmp = ngx_queue_data(q, gevent_cothread, op);
        assert(tmp->state == GEVENT_COTHREAD_READY);
        assert(tmp->stacklet);
        ngx_queue_remove(q);
        _before_switch(hub, tmp);
        return tmp->stacklet;
    }
    if (hub->stacklet) {
        assert(hub->current);
        _before_switch(hub, NULL);
        return hub->stacklet;
    }
    assert(hub->main.stacklet);
    _before_switch(hub, &hub->main);
    return hub->main.stacklet;
}


stacklet_handle hub_starter_fn(stacklet_handle source, void* data) {
    gevent_hub* hub = (gevent_hub*)data;
    assert(source);
    _after_switch(hub, NULL, source);
    hub->loop_alive = 1;
    uv_run(hub->loop);
    hub->loop_alive = 0;
    /* this is to make sure _get_next_stacklet won't return hub->stacklet */
    hub->stacklet = NULL;
    return _get_next_stacklet(hub);
}


static stacklet_handle cothread_starter_fn(stacklet_handle source, gevent_cothread* g) {
    gevent_hub* hub = g->hub;
    gevent_cothread_fn run = g->op.init.run;
    assert(source);
    assert(run);
    assert(g->state == GEVENT_COTHREAD_NEW);
    _after_switch(hub, g, source);
    run(g);
    g->stacklet = NULL;  /* for debugging */
    return _get_next_stacklet(hub);
}


/* Switch to another cothread.
 * Can be used from anywhere - hub or cothread.
 * Upon exit hub->current->state is guaranteed to be GEVENT_COTHREAD_CURRENT.
 */
static void gevent_switch(gevent_cothread* t) {
    stacklet_handle source;
    gevent_hub* hub = t->hub;
    gevent_cothread* current = hub->current;
    assert(t != current);

    assert(t->state != GEVENT_COTHREAD_CURRENT);
    assert(t->state != GEVENT_COTHREAD_DEAD);
    assert(t->stacklet != EMPTY_STACKLET_HANDLE);

    _before_switch(hub, t);

    if (t->state == GEVENT_COTHREAD_NEW) {
        assert(t->op.init.run);
        /* XXX maybe it's possible to pre-allocate memory, so that stacklet_new never fails
           XXX see what latest pypy does */
        source = stacklet_new(hub->thread, (stacklet_run_fn)cothread_starter_fn, t);
    }
    else {
        assert(t->stacklet);
        source = stacklet_switch(hub->thread, t->stacklet);
    }

    if (!source) {
        assert(!"gevent_switch() failed");
        abort();
    }

    _after_switch(hub, current, source);
}


void gevent_switch_to_hub(gevent_hub* hub) {
    stacklet_handle source;
    gevent_cothread* current = hub->current;
    assert(current);

    _before_switch(hub, NULL);

    if (hub->stacklet) {
        assert(hub->stacklet != EMPTY_STACKLET_HANDLE);
        source = stacklet_switch(hub->thread, hub->stacklet);
    }
    else {
        source = stacklet_new(hub->thread, hub_starter_fn, hub);
    }

    if (!source) {
        assert(!"gevent_switch_to_hub() failed");
        abort();
    }

    _after_switch(hub, current, source);
}


void gevent_yield(gevent_hub* hub) {
    ngx_queue_t* q;
    gevent_cothread* tmp;
    if (!ngx_queue_empty(&hub->ready)) {
        q = ngx_queue_head(&hub->ready);
        tmp = ngx_queue_data(q, gevent_cothread, op);
        assert(tmp->state == GEVENT_COTHREAD_READY);
        ngx_queue_remove(q);
        return gevent_switch(tmp);
    }
    return gevent_switch_to_hub(hub);
}


static void sleep_cb(uv_timer_t* handle, int status) {
    gevent_cothread* paused = ngx_queue_data(handle, gevent_cothread, op);
    assert(handle);
    assert(status == 0);
    assert(paused->state == GEVENT_WAITING_TIMER);
    assert(paused->hub);
    assert(!paused->hub->current);
    gevent_switch(paused);
    /* QQQ do we need to check if switch is still needed?
    // QQQ in some cases - no: sleep() stops the timer and that must clear pending events
    // QQQ in some cases - yes: there's no way to cancel getaddrinfo
    // QQQ counter check? a small counter that is increased for each operation;
    //     it is also stored in uv_timer_t
    */
}


static int _sleep_internal(gevent_hub* hub, int64_t timeout, int ref) {
    gevent_cothread* current = get_current(hub);
    uv_timer_t* ponce = &hub->current->op.timer;

    if (uv_timer_init(hub->loop, ponce))
        return -1;

    if (uv_timer_start(ponce, sleep_cb, timeout, 0))
        return -1;

    if (!ref)
        uv_unref((uv_handle_t*)ponce);

    current->state = GEVENT_WAITING_TIMER;
    gevent_yield(hub);
    if (uv_timer_stop(ponce)) {
        assert(!"uv_timer_stop returned non-zero");
        return -1;
    }
    return 0;
}


int gevent_sleep(gevent_hub* hub, int64_t timeout) {
    return _sleep_internal(hub, timeout, 1);
}


int gevent_wait(gevent_hub* hub, int64_t timeout) {
    if (timeout > 0)
        return _sleep_internal(hub, timeout, 0);
    gevent_yield(hub);
    return 0;
}


/* channels */

void gevent_channel_init(gevent_hub* hub, gevent_channel* ch) {
    ngx_queue_init(&ch->receivers);
    ngx_queue_init(&ch->senders);
    ch->hub = hub;
}


void gevent_channel_receive(gevent_channel* ch, void** result) {
    gevent_cothread* current = get_current(ch->hub);
    /* what if current is NULL? */
    if (ngx_queue_empty(&ch->senders)) {
        /* no senders, has to block */
        ngx_queue_t* q = &current->op.channel.queue;
        ngx_queue_init(q);
        ngx_queue_insert_tail(&ch->receivers, q);
        current->state = GEVENT_COTHREAD_CHANNEL_R;
        gevent_yield(ch->hub);
        ngx_queue_remove(q);
        *result = current->op.channel.value;
    }
    else {
        /* a blocked sender is available - take its value and unlock it */
        ngx_queue_t* sender_q = ngx_queue_head(&ch->senders);
        gevent_cothread* sender_cothread = ngx_queue_data(sender_q, gevent_cothread, op);
        assert(sender_cothread->state == GEVENT_COTHREAD_CHANNEL_S);
        *result = sender_cothread->op.channel.value;
        _activate(current);
        gevent_switch(sender_cothread);
    }
}


void gevent_channel_send(gevent_channel* ch, void* value) {
    gevent_cothread* current = get_current(ch->hub);
    /* what if current is NULL? */
    if (ngx_queue_empty(&ch->receivers)) {
        /* no receivers, has to block */
        ngx_queue_t* q = &current->op.channel.queue;
        ngx_queue_init(q);
        ngx_queue_insert_tail(&ch->senders, q);
        current->op.channel.value = value;
        current->state = GEVENT_COTHREAD_CHANNEL_S;
        gevent_yield(ch->hub);
        ngx_queue_remove(q);
    }
    else {
        ngx_queue_t* receiver_q = ngx_queue_head(&ch->receivers);
        gevent_cothread* receiver_cothread = ngx_queue_data(receiver_q, gevent_cothread, op);
        assert(receiver_cothread->state == GEVENT_COTHREAD_CHANNEL_R);
        receiver_cothread->op.channel.value = value;
        _activate(current);
        gevent_switch(receiver_cothread);
    }
}

/* TODO: channel operation should switch to hub once in a while */

/* getaddrinfo */

static void getaddrinfo_cb(uv_getaddrinfo_t* req, int error, struct addrinfo* res) {
    gevent_cothread* my = (gevent_cothread*)req->data;
    free(req);
    if (my) {
        assert(my->state == GEVENT_WAITING_GETADDRINFO);
        my->op.getaddrinfo.res = res;
        my->op.getaddrinfo.error = error;
        gevent_switch(my);
    }
    else {
        if (!error && res)
            uv_freeaddrinfo(res);
    }
}


int gevent_getaddrinfo(gevent_hub* hub,
                       const char *node,
                       const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res) {
    struct gevent_cothread_getaddrinfo_s* g;
    int retcode;
    gevent_cothread* current;
    current = get_current(hub);

    g = &current->op.getaddrinfo;

    g->req = malloc(sizeof(uv_getaddrinfo_t));

    if (!g->req)
        return set_artificial_error(hub->loop, UV_ENOMEM);

    retcode = uv_getaddrinfo(hub->loop, g->req, getaddrinfo_cb, node, service, hints);

    if (retcode) {
        free(g->req);
        return retcode;
    }

    /* uv_getaddrinfo succeeded, now it's a responsibility of getaddrinfo_cb to free req */

    g->req->data = current;
    current->state = GEVENT_WAITING_GETADDRINFO;

    gevent_yield(hub);

    if (!g->error)
        *res = g->res;

    return g->error;
}
