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
    ngx_queue_init(&t->ready);
    t->hub = hub;
    t->state = GEVENT_COTHREAD_NEW;
    t->current_op = NULL;
    t->stacklet = NULL; /* not needed, debug only */
    t->op.run = run;
    t->op_status = 0;
}


static gevent_cothread* get_current(gevent_hub* hub) {
    gevent_cothread* current;
    assert(hub);
    current = hub->current;
    assert(current);
    assert(current->state == GEVENT_COTHREAD_CURRENT);
    return current;
}


static void _put_into_ready(gevent_cothread* t) {
    if (t->state == GEVENT_COTHREAD_CURRENT) {
        t->state = GEVENT_COTHREAD_READY;
        ngx_queue_insert_tail(&t->hub->ready, &t->ready);
    }
}


int gevent_cothread_spawn(gevent_cothread* t) {
    gevent_cothread* current = get_current(t->hub);
    if (current) {
        _put_into_ready(current);
    }
    return gevent_switch(t);
}


void _before_switch(gevent_hub* hub, gevent_cothread* new) {
    assert(hub);
    /* fprintf(stderr, "_before_switch hub=%x from=%x to=%x\n", (unsigned)hub, (unsigned)hub->current, (unsigned)new); */
    assert(new || hub->current);
}


void _after_switch(gevent_hub* hub, gevent_cothread* new, stacklet_handle source) {
    gevent_cothread* whodied = NULL;
    /* fprintf(stderr, "_after_switch hub=%x from=%x to=%x stacklet=%x\n", (unsigned)hub, (unsigned)hub->current, (unsigned)new, (unsigned)source); */
    assert(source);
    if (hub->current) {
        /* we switched from another greenlet */
        if (source == EMPTY_STACKLET_HANDLE) {
            assert(hub->current != &hub->main);
            assert(hub->current->state == GEVENT_COTHREAD_CURRENT);
            hub->current->state = GEVENT_COTHREAD_DEAD;
            whodied = hub->current;
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
        /* when cothread is CURRENT, stacklet is garbage */
        new->stacklet = NULL;
    }
    else {
        /* when hub is active (hub->current==NULL), hub->stacklet is garbage */
        hub->stacklet = NULL;
    }
    if (whodied) {
        hub->exit_fn(whodied);
    }
}


stacklet_handle _get_next_stacklet(gevent_hub* hub) {
    ngx_queue_t* q;
    gevent_cothread* tmp;
    if (!ngx_queue_empty(&hub->ready)) {
        q = ngx_queue_head(&hub->ready);
        tmp = ngx_queue_data(q, gevent_cothread, ready);
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


static int gevent_switch(gevent_cothread* t) {
    stacklet_handle source;
    assert(t);
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

    if (!source)
        return set_artificial_error(hub->loop, GEVENT_ENOMEM);

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

    if (!source)
        return set_artificial_error(hub->loop, GEVENT_ENOMEM);

    _after_switch(hub, current, source);
    return 0;
}


int gevent_yield(gevent_hub* hub) {
    ngx_queue_t* q;
    gevent_cothread* tmp;
    if (!ngx_queue_empty(&hub->ready)) {
        q = ngx_queue_head(&hub->ready);
        tmp = ngx_queue_data(q, gevent_cothread, ready);
        assert(tmp->state == GEVENT_COTHREAD_READY);
        ngx_queue_remove(q);
        return gevent_switch(tmp);
    }
    return gevent_switch_to_hub(hub);
}


static void switch_cb(uv_timer_t* handle, int status) {
    gevent_cothread* paused = (gevent_cothread*)handle->data;
    assert(paused);
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
    int retcode = -1;
    gevent_cothread* current = get_current(hub);
    uv_timer_t* ponce = &hub->current->op.timer;

    if (uv_timer_init(hub->loop, ponce))
        return -1;

    if (uv_timer_start(ponce, switch_cb, timeout, 0))
        return -1;

    if (!ref)
        uv_unref((uv_handle_t*)ponce);

    current->state = GEVENT_WAITING_TIMER;
    ponce->data = current;
    retcode = gevent_yield(hub);
    if (uv_timer_stop(ponce) < 0) {
        /* this cannot happen */
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
    if (blocked_cothread->current_op != req) {
        /* it still could be different request; 2 getaddrinfo requests in a row
         * would use the same current_op beccause it is stored in the same storage */
        return;
    }
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
    if (ngx_queue_empty(&ch->senders)) {
        /* no senders - block */
        ngx_queue_t* q = &current->op.channel.queue;
        ngx_queue_init(q);
        ngx_queue_insert_tail(&ch->receivers, q);
        current->state = GEVENT_COTHREAD_CHANNEL_R;
        retcode = gevent_switch_to_hub(ch->hub);
        /* check that this was a switch from gevent_channel_send() call in another cothread */
        /* XXX XXX ngx_queue_empty does not really show removed things XXX XXX */
        if (ngx_queue_empty(q)) {
            /* it looks like a legit switch XXX do the same check */
            *result = current->op.channel.value;
            assert(retcode == 0);
            return 0;
        }
        else {
            ngx_queue_remove(q);
            if (!retcode)
                retcode = set_artificial_error(ch->hub->loop, GEVENT_EINTERRUPTED);
            return retcode;
        }
    }
    else {
        /* a blocked sender is available - take its value and unlock it */
        ngx_queue_t* sender = ngx_queue_head(&ch->senders);
        gevent_cothread* sender_cothread = ngx_queue_data(sender, gevent_cothread, op);
        assert(sender_cothread->state == GEVENT_COTHREAD_CHANNEL_S);
        ngx_queue_remove(sender);
        current->state = GEVENT_COTHREAD_CHANNEL_S;
        *result = sender_cothread->op.channel.value;
        /* save to next  */
        return gevent_switch(sender_cothread);
    }
    return retcode;
}


int gevent_channel_send(gevent_channel* ch, void* value) {
    int retcode;
    gevent_cothread* current = get_current(ch->hub);
    if (ngx_queue_empty(&ch->receivers)) {
        ngx_queue_t* q = &current->op.channel.queue;
        ngx_queue_init(q);
        ngx_queue_insert_tail(&ch->senders, q);
        current->op.channel.value = value;
        current->state = GEVENT_COTHREAD_CHANNEL_S;
        return gevent_switch_to_hub(ch->hub);
    }
    else {
        ngx_queue_t* receiver = ngx_queue_head(&ch->receivers);
        gevent_cothread* receiver_cothread = ngx_queue_data(receiver, gevent_cothread, op);
        assert(receiver_cothread->state == GEVENT_COTHREAD_CHANNEL_R);
        ngx_queue_remove(receiver);
        receiver_cothread->op.channel.value = value;
        return gevent_switch(receiver_cothread);
    }
    return retcode;
}

/* TODO: channel operation should switch to hub once in a while */
