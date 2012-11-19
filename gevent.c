#include "gevent.h"
#include "stacklet/stacklet.c"
#include <stdio.h>

static gevent_hub _default_hub;
static gevent_hub* default_hub_handle = NULL;


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


void gevent_cothread_init(gevent_hub* hub, gevent_cothread* t, gevent_cothread_fn run) {
    ngx_queue_init(&t->spawned);
    t->hub = hub;
    t->stacklet = NULL;
    t->state = GEVENT_COTHREAD_NEW;
    t->op.run = run;
}

static void _switch(gevent_cothread* g);

static void hub_spawn_fn(uv_timer_t* handle, int status) {
    assert(status == 0);
    gevent_hub* hub = ngx_queue_data(handle, gevent_hub, timer);
    while (!ngx_queue_empty(&hub->spawned)) {
        gevent_cothread* pt = ngx_queue_data(ngx_queue_head(&hub->spawned), gevent_cothread, spawned);
        _switch(pt);
    }
    uv_timer_stop(&hub->timer);
}


void gevent_activate(gevent_cothread* t) {
    if (!ngx_queue_empty(&t->spawned))
        return;
    ngx_queue_insert_tail(&t->hub->spawned, &t->spawned);
    uv_timer_start(&t->hub->timer, hub_spawn_fn, 0, 0);
}


void gevent_spawn(gevent_hub* hub, gevent_cothread* t, gevent_cothread_fn run) {
    gevent_cothread_init(hub, t, run);
    gevent_activate(t);
}


int gevent_hub_init(gevent_hub* hub, uv_loop_t* loop)
{
    ngx_queue_init(&hub->spawned);
    hub->loop = loop;
    hub->thread = stacklet_newthread();
    if (!hub->thread) {
        /* XXX check if malloc sets errno; use it if it does
           XXX return errno if set otherwise UV_ENOMEM?
        */
        return set_artificial_error(hub->loop, GEVENT_ENOMEM);
    }
    hub->stacklet = NULL;
    hub->current = &hub->main;
    gevent_cothread_init(hub, &hub->main, NULL);
    hub->main.state = GEVENT_COTHREAD_CURRENT;
    if (uv_timer_init(hub->loop, &hub->timer))
        return -1;
    return 0;
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
    return hub->main.stacklet;
}


int gevent_yield(gevent_hub* hub) {
    stacklet_handle source;
    assert(hub->thread);
    if (hub->stacklet) {
        assert(hub->stacklet != EMPTY_STACKLET_HANDLE);
        source = stacklet_switch(hub->thread, hub->stacklet);
    }
    else {
        source = stacklet_new(hub->thread, hub_run, hub);
    }
    if (!source)
        return set_artificial_error(hub->loop, GEVENT_ENOMEM);
    // source is the new stacklet for hub or EMPTY_STACKLET_HANDLE if uv_loop is done
    if (source == EMPTY_STACKLET_HANDLE) {
        // hub has finished
        hub->stacklet = NULL;
        return set_artificial_error(hub->loop, GEVENT_ENOLOOP);
    }
    hub->stacklet = source;
    return 0;
}


static stacklet_handle starter_fn(stacklet_handle source, gevent_cothread* g) {
    gevent_cothread_fn run = g->op.run;
    assert(run);
    assert(g->state == GEVENT_COTHREAD_NEW);
    g->state = GEVENT_COTHREAD_CURRENT;
    g->hub->stacklet = source;
    run(g);
    return g->hub->stacklet;
}


// XXX rename to gevent_switch
static void _switch(gevent_cothread* g) {
    stacklet_handle source;
    assert(g);
    gevent_hub* hub = g->hub;
    assert(hub->thread);
    if (!ngx_queue_empty(&g->spawned)) {
        ngx_queue_remove(&g->spawned);
    }

    hub->current = g;

    if (g->state == GEVENT_COTHREAD_NEW) {
        assert(g->op.run);
        source = stacklet_new(hub->thread, (stacklet_run_fn)starter_fn, g);
    }
    else if (g->stacklet) {
        assert(g->stacklet != EMPTY_STACKLET_HANDLE);
        source = stacklet_switch(hub->thread, g->stacklet);
    }
    else
        return;

    // source is the stacklet that just switched to the hub
    if (source == EMPTY_STACKLET_HANDLE) {
        // current has finished
        hub->current->stacklet = NULL;
        hub->current->state = GEVENT_COTHREAD_DEAD;
        // QQQ python-gevent will probably need something like this
        // if (hub->exit_cb) hub->exit_cb(hub->current);
    }
    hub->current->stacklet = source;
}


static void switch_cb(uv_timer_t* handle, int status) {
    gevent_cothread* paused = (gevent_cothread*)handle->data;
    assert(paused);
    // QQQ pass status along; check for which watchers it actually matters
    // paused->op_status = status;
    _switch(paused);
    // QQQ do we need to check if switch is still needed?
    // QQQ in some cases - no: sleep() stops the timer and that must clear pending events
    // QQQ in some cases - yes: there's no way to cancel getaddrinfo
    // QQQ counter check? a small counter that is increased for each operation;
    //     it is also stored in uv_timer_t
}


inline gevent_cothread* get_current(gevent_hub* hub) {
    gevent_cothread* current;
    assert(hub);
    current = hub->current;
    assert(current);
    assert(current->state == 0);
    return current;
}

#define YIELD(HUB, STATE) do {            \
    current->state = STATE;            \
    retcode = gevent_yield(HUB);          \
    assert(current->state == STATE);   \
    current->state = 0;                \
    } while(0);



static int _sleep_internal(gevent_hub* hub, int64_t timeout, int ref) {
    int retcode;
    gevent_cothread* current = get_current(hub);
    uv_timer_t* ponce = &hub->current->op.timer;
    if (uv_timer_init(hub->loop, ponce))
        return -1;
    if (uv_timer_start(ponce, switch_cb, timeout, 0))
        return -1;
    if (!ref)
        uv_unref((uv_handle_t*)ponce);
    ponce->data = current;
    YIELD(hub, GEVENT_WAITING_TIMER);
    // XXX must check that it is actually, this timer's callback
    if (uv_timer_stop(ponce) < 0)
        // XXX this cannot happen, but if it could, this means switch would occur later
        retcode = -1;
    //if (current->state != GEVENT_WAITING_TIMER) {
    //    // XXX this is not right
    //    retcode = set_artificial_error(hub->loop, GEVENT_EINTERRUPTED);
    // }
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
        retcode = gevent_yield(hub);
    if (retcode == -1 && (gevent_err_code)uv_last_error(hub->loop).code == GEVENT_ENOLOOP) {
        retcode = 0;
    }
    return retcode;
}


static void getaddrinfo_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    gevent_cothread* current = (gevent_cothread*)req->data;
    if (!current)
        return;
    if (current->state != GEVENT_WAITING_GETADDRINFO)
        return;
    // if container_of(req, gevent_cothread) != current
    // it's a different getaddrinfo already
    current->op_status = status;
    *current->op.getaddrinfo.result = res;
    _switch(current);
}


int gevent_getaddrinfo(gevent_hub* hub,
                       const char *node,
                       const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res) {
    int retcode;
    gevent_cothread* current = get_current(hub);
    uv_getaddrinfo_t* req = &current->op.getaddrinfo.req;
    retcode = uv_getaddrinfo(hub->loop, req, getaddrinfo_cb, node, service, hints);
    if (retcode)
        return retcode;
    req->data = current;
    current->op.getaddrinfo.result = res;
    YIELD(hub, GEVENT_WAITING_GETADDRINFO);
    req->data = NULL;
    return retcode || current->op_status;
}


int gevent_channel_init(gevent_hub* hub, gevent_channel* ch) {
    ngx_queue_init(&ch->receivers);
    ngx_queue_init(&ch->senders);
    ch->hub = hub;
    if (uv_timer_init(hub->loop, &ch->timer))
        return -1;
    return 0;
}


static void channel_timer_fn(uv_timer_t* handle, int status) {
    fprintf(stderr, "channel_timer_t\n");
    gevent_channel* ch = ngx_queue_data(handle, gevent_channel, timer);
    while (!ngx_queue_empty(&ch->receivers) && !ngx_queue_empty(&ch->senders)) {
        fprintf(stderr, "channel_timer - xxx\n");
        ngx_queue_t* receiver = ngx_queue_head(&ch->receivers);
        ngx_queue_t* sender = ngx_queue_head(&ch->senders);
        ngx_queue_remove(receiver);
        ngx_queue_remove(sender);
        gevent_cothread* receiver_t = ngx_queue_data(receiver, gevent_cothread, op);
        gevent_cothread* sender_t = ngx_queue_data(sender, gevent_cothread, op);
        receiver_t->op.channel.value = sender_t->op.channel.value;
        _switch(receiver_t);
        _switch(sender_t);
    }
    uv_timer_stop(&ch->timer);
    fprintf(stderr, "channel_timer_fn - done\n");
}


int gevent_channel_receive(gevent_channel* ch, void** result) {
    int retcode;
    gevent_cothread* current = get_current(ch->hub);
    ngx_queue_t* q = &current->op.channel.queue;
    ngx_queue_init(q);
    ngx_queue_insert_tail(&ch->receivers, q);
    uv_timer_start(&ch->timer, channel_timer_fn, 0, 0);
    fprintf(stderr, "gevent_channel_receive before yield\n");
    YIELD(ch->hub, GEVENT_CHANNEL_RECV);
    fprintf(stderr, "gevent_channel_receive after yield\n");
    ngx_queue_remove(q);
    if (retcode)
        return retcode;
    *result = current->op.channel.value;
    return 0;
}


int gevent_channel_send(gevent_channel* ch, void* value) {
    int retcode;
    gevent_cothread* current = get_current(ch->hub);
    ngx_queue_t* q = &current->op.channel.queue;
    ngx_queue_init(q);
    ngx_queue_insert_tail(&ch->senders, q);
    uv_timer_start(&ch->timer, channel_timer_fn, 0, 0);
    current->op.channel.value = value;
    fprintf(stderr, "gevent_channel_send before yield\n");
    YIELD(ch->hub, GEVENT_CHANNEL_SEND);
    fprintf(stderr, "gevent_channel_send after yield\n");
    ngx_queue_remove(q);
    if (retcode)
        return retcode;
    return 0;
}
