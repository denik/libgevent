#include "gevent.h"
#include "stacklet/stacklet.c"
#include <stdio.h>

static gevent_hub default_hub_struct;
static gevent_hub* default_hub_ptr = NULL;

static int gevent_switch(gevent_cothread* t);


void gevent_noop(gevent_cothread* t) {
    /* this is what hub->exit_fn is initilized with */
}


#define UV_ERR_NAME_GEN(val, name, s) case UV_##name : return #name;
#define GEVENT_ERR_NAME_GEN(val, name, s) case GEVENT_##name : return #name;
const char* gevent_err_name(int code) {
  switch (code) {
    UV_ERRNO_MAP(UV_ERR_NAME_GEN)
    GEVENT_ERRNO_MAP(GEVENT_ERR_NAME_GEN)
    default:
      assert(0);
      return NULL;
  }
}
#undef UV_ERR_NAME_GEN
#undef GEVENT_ERR_NAME_GEN


#define UV_STRERROR_GEN(val, name, s) case UV_##name : return s;
#define GEVENT_STRERROR_GEN(val, name, s) case GEVENT_##name : return s;
const char* gevent_strerror(int code) {
  switch (code) {
    UV_ERRNO_MAP(UV_STRERROR_GEN)
    GEVENT_ERRNO_MAP(GEVENT_STRERROR_GEN)
    default:
      return "Unknown system error";
  }
}
#undef UV_STRERROR_GEN
#undef GEVENT_STRERROR_GEN


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
    ngx_queue_init(&hub->ready);
    hub->loop = loop;
    hub->thread = stacklet_newthread();
    if (!hub->thread) {
        /* XXX check if malloc sets errno; use it if it does
           XXX return errno if set otherwise UV_ENOMEM?
        */
        return set_artificial_error(hub->loop, GEVENT_ENOMEM);
    }
    hub->stacklet = NULL;
    gevent_cothread_init(hub, &hub->main, NULL);
    hub->main.state = GEVENT_COTHREAD_CURRENT;
    hub->current = &hub->main;
    hub->exit_fn = gevent_noop;
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
    t->op.run = run;
    t->op_status = 0;
    t->stacklet = NULL; /* not needed, debug only */
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


int gevent_cothread_spawn(gevent_cothread* t) {
    gevent_cothread* current = get_current(t->hub);
    if (current) {
        _activate(current);
    }
    assert(t->state == GEVENT_COTHREAD_NEW);
    return gevent_switch(t);
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
        /* we switched from another greenlet */
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
    if (who_died) {
        hub->exit_fn(who_died);
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
    uv_run(hub->loop);
    /* this is to make sure _get_next_stacklet won't return hub->stacklet */
    hub->stacklet = NULL;
    return _get_next_stacklet(hub);
}


static stacklet_handle cothread_starter_fn(stacklet_handle source, gevent_cothread* g) {
    gevent_hub* hub = g->hub;
    gevent_cothread_fn run = g->op.run;
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
static int gevent_switch(gevent_cothread* t) {
    stacklet_handle source;
    gevent_hub* hub = t->hub;
    gevent_cothread* current = hub->current;
    assert(t != current);

    assert(t->state != GEVENT_COTHREAD_CURRENT);
    assert(t->state != GEVENT_COTHREAD_DEAD);
    assert(t->stacklet != EMPTY_STACKLET_HANDLE);

    _before_switch(hub, t);

    if (t->state == GEVENT_COTHREAD_NEW) {
        assert(t->op.run);
        source = stacklet_new(hub->thread, (stacklet_run_fn)cothread_starter_fn, t);
    }
    else {
        assert(t->stacklet);
        source = stacklet_switch(hub->thread, t->stacklet);
    }

    if (!source) {
        if (current) {
            /* it's easier for callers to know that state of current is always good */
            current->state = GEVENT_COTHREAD_CURRENT;
        }
        return set_artificial_error(hub->loop, GEVENT_ENOMEM);
    }

    _after_switch(hub, current, source);
    return 0;
}


int gevent_switch_to_hub(gevent_hub* hub) {
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
        if (current) {
            current->state = GEVENT_COTHREAD_CURRENT;
        }
        return set_artificial_error(hub->loop, GEVENT_ENOMEM);
    }

    _after_switch(hub, current, source);
    return 0;
}


int gevent_yield(gevent_hub* hub) {
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
    if (gevent_switch(paused)) {
        assert(!"gevent_switch() failed");
    }
    /* QQQ do we need to check if switch is still needed?
    // QQQ in some cases - no: sleep() stops the timer and that must clear pending events
    // QQQ in some cases - yes: there's no way to cancel getaddrinfo
    // QQQ counter check? a small counter that is increased for each operation;
    //     it is also stored in uv_timer_t
    */
}


/* this should be the external API ?? */
static int _sleep_internal(gevent_hub* hub, int64_t timeout, int ref) {
    int retcode;
    gevent_cothread* current = get_current(hub);
    uv_timer_t* ponce = &hub->current->op.timer;

    if (uv_timer_init(hub->loop, ponce))
        return -1;

    if (uv_timer_start(ponce, sleep_cb, timeout, 0))
        return -1;

    if (!ref)
        uv_unref((uv_handle_t*)ponce);

    current->state = GEVENT_WAITING_TIMER;
    retcode = gevent_yield(hub);
    if (uv_timer_stop(ponce)) {
        assert(!"uv_timer_stop returned non-zero");
        return -1;
    }
    return retcode;
}


int gevent_sleep(gevent_hub* hub, int64_t timeout) {
    return _sleep_internal(hub, timeout, 1);
}


int gevent_wait(gevent_hub* hub, int64_t timeout) {
    int retcode;
    if (timeout > 0)
        retcode = _sleep_internal(hub, timeout, 0);
    else
        retcode = gevent_switch_to_hub(hub);
    if (retcode == -1 && (gevent_err_code)uv_last_error(hub->loop).code == GEVENT_ENOLOOP) {
        retcode = 0;
    }
    return retcode;
}


static void getaddrinfo_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    gevent_cothread* blocked_cothread = (gevent_cothread*)req->data;
    if (!blocked_cothread)
        return;
    if (blocked_cothread->state != GEVENT_WAITING_GETADDRINFO)
        return;
    *blocked_cothread->op.getaddrinfo.result = res;
    if (gevent_switch(blocked_cothread)) {
        assert(!"gevent_switch() failed");
    }
}


int gevent_getaddrinfo(gevent_hub* hub,
                       const char *node,
                       const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res) {
    int retcode;
    gevent_cothread* current;
    uv_getaddrinfo_t* req;
    current = get_current(hub);
    req = &current->op.getaddrinfo.req;
    retcode = uv_getaddrinfo(hub->loop, req, getaddrinfo_cb, node, service, hints);
    if (retcode)
        return retcode;
    req->data = current;
    current->op.getaddrinfo.result = res;
    current->state = GEVENT_WAITING_GETADDRINFO;
    retcode = gevent_switch_to_hub(hub);
    req->data = NULL;
    return retcode || current->op_status;
}


int gevent_channel_init(gevent_hub* hub, gevent_channel* ch) {
    ngx_queue_init(&ch->receivers);
    ngx_queue_init(&ch->senders);
    ch->hub = hub;
    return 0;
}


int gevent_channel_receive(gevent_channel* ch, void** result) {
    int retcode;
    gevent_cothread* current = get_current(ch->hub);
    /* what if current is NULL? */
    if (ngx_queue_empty(&ch->senders)) {
        /* no senders, has to block */
        ngx_queue_t* q = &current->op.channel.queue;
        ngx_queue_init(q);
        ngx_queue_insert_tail(&ch->receivers, q);
        current->state = GEVENT_COTHREAD_CHANNEL_R;
        retcode = gevent_yield(ch->hub);
        ngx_queue_remove(q);
        if (retcode)
            return retcode;
        *result = current->op.channel.value;
        return 0;
    }
    else {
        /* a blocked sender is available - take its value and unlock it */
        ngx_queue_t* sender_q = ngx_queue_head(&ch->senders);
        gevent_cothread* sender_cothread = ngx_queue_data(sender_q, gevent_cothread, op);
        assert(sender_cothread->state == GEVENT_COTHREAD_CHANNEL_S);
        *result = sender_cothread->op.channel.value;
        _activate(current);
        return gevent_switch(sender_cothread);
    }
    return retcode;
}


int gevent_channel_send(gevent_channel* ch, void* value) {
    int retcode;
    gevent_cothread* current = get_current(ch->hub);
    /* what if current is NULL? */
    if (ngx_queue_empty(&ch->receivers)) {
        /* no receivers, has to block */
        ngx_queue_t* q = &current->op.channel.queue;
        ngx_queue_init(q);
        ngx_queue_insert_tail(&ch->senders, q);
        current->op.channel.value = value;
        current->state = GEVENT_COTHREAD_CHANNEL_S;
        retcode = gevent_yield(ch->hub);
        ngx_queue_remove(q);
        return retcode;
    }
    else {
        ngx_queue_t* receiver_q = ngx_queue_head(&ch->receivers);
        gevent_cothread* receiver_cothread = ngx_queue_data(receiver_q, gevent_cothread, op);
        assert(receiver_cothread->state == GEVENT_COTHREAD_CHANNEL_R);
        receiver_cothread->op.channel.value = value;
        _activate(current);
        return gevent_switch(receiver_cothread);
    }
    return retcode;
}

/* TODO: channel operation should switch to hub once in a while */
