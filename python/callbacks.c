#ifdef WITH_THREAD
#define GIL_DECLARE  PyGILState_STATE ___save
#define GIL_ENSURE  ___save = PyGILState_Ensure();
#define GIL_RELEASE  PyGILState_Release(___save);
#else
#define GIL_DECLARE
#define GIL_ENSURE
#define GIL_RELEASE
#endif


static void cothread_run(gevent_cothread* t) {
    GIL_DECLARE;
    PyObject *result, *run, *args, *kwargs;
    GIL_ENSURE;
    assert(t->state == GEVENT_COTHREAD_CURRENT);

    run = t->op.init.args[0];
    args = t->op.init.args[1];
    kwargs = t->op.init.args[2];
    Py_INCREF(run);
    Py_INCREF(args);
    Py_XINCREF(kwargs);
    /* we do not incref this objects because they already we increfed by caller */

    result = PyObject_Call(run, args, kwargs);
    if (result) {
        Py_DECREF(result);
    }
    else {
        PyErr_Print();
        PyErr_Clear();
    }

    Py_DECREF(run);
    Py_DECREF(args);
    Py_XDECREF(kwargs);
    GIL_RELEASE;
}


static void cothread_exit_fn(gevent_cothread* t) {
    GIL_DECLARE;
    PyObject* py_cothread;
    GIL_ENSURE;
    py_cothread = (PyObject*)ngx_queue_data(t, struct PyGeventCothreadObject, data);
    Py_DECREF(py_cothread);
    GIL_RELEASE;
}


static void cothread_init(gevent_cothread* t, PyObject* run, PyObject* args, PyObject* kwargs) {
    gevent_cothread_init(gevent_default_hub(), t, cothread_run);
    t->op.init.args[0] = run;
    Py_INCREF(run);

    t->op.init.args[1] = args;
    Py_INCREF(args);

    t->op.init.args[2] = kwargs;
    Py_INCREF(kwargs);
}
