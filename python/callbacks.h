static void cothread_run(gevent_cothread* t);
static void cothread_exit_fn(gevent_cothread* t);
static void cothread_init(gevent_cothread* t, PyObject* run, PyObject* args, PyObject* kwargs);
