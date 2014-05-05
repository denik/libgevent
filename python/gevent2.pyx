import sys
from cpython cimport *


cdef extern from "../gevent.c":
    int GEVENT_COTHREAD_CURRENT
    int GEVENT_COTHREAD_NEW
    int GEVENT_COTHREAD_READY
    int GEVENT_COTHREAD_DEAD
    int GEVENT_COTHREAD_CHANNEL_R
    int GEVENT_COTHREAD_CHANNEL_S

    ctypedef struct gevent_cothread:
        int state
        void* exit_fn

    ctypedef struct gevent_hub:
        pass

    ctypedef struct gevent_channel:
        pass

    ctypedef struct gevent_semaphore:
        int counter

    gevent_hub* gevent_default_hub()
    void gevent_cothread_init(gevent_hub*, gevent_cothread*, void*)
    void gevent_cothread_spawn(gevent_cothread*)

    int gevent_sleep(gevent_hub* hub, long timeout)
    int gevent_wait(gevent_hub* hub, long timeout)

    void gevent_channel_init(gevent_hub* hub, gevent_channel* ch)
    void gevent_channel_send(gevent_channel* ch, object value)
    void gevent_channel_receive(gevent_channel* ch, void** result)

    void gevent_semaphore_init(gevent_hub* hub, gevent_semaphore* sem, int counter)
    int gevent_semaphore_acquire(gevent_semaphore* sem)
    int gevent_semaphore_release(gevent_semaphore* sem)


cdef extern from "socketmodule.c":
    object pygevent_getaddrinfo(object)


cdef extern from "callbacks.h":
    void cothread_init(gevent_cothread* t, object run, object args, object kwargs)
    void cothread_exit_fn(gevent_cothread* t)


cdef public class cothread [object PyGeventCothreadObject, type PyGeventCothread_Type]:
    cdef gevent_cothread data

    def __cinit__(self, run, *args, **kwargs):
        cothread_init(&self.data, run, args, kwargs)

    cpdef spawn(self):
        if self.data.state != GEVENT_COTHREAD_NEW:
            raise ValueError('This cothread already was spawned')
        Py_INCREF(self)
        self.data.exit_fn = <void*>cothread_exit_fn
        gevent_cothread_spawn(&self.data)

    property state:

        def __get__(self):
            return self.data.state


cdef public class channel [object PyGeventChannelObject, type PyGeventChannel_Type]:
    cdef gevent_channel data

    def __cinit__(self):
        gevent_channel_init(gevent_default_hub(), &self.data)

    def send(self, object item):
        gevent_channel_send(&self.data, item)

    def receive(self):
        cdef void* obj
        gevent_channel_receive(&self.data, &obj)
        return <object>obj


cdef public class semaphore [object PyGeventSemaphoreObject, type PyGeventSemaphore_Type]:
    cdef gevent_semaphore data

    def __cinit__(self, int count):
        gevent_semaphore_init(gevent_default_hub(), &self.data, count)

    def acquire(self):
        gevent_semaphore_acquire(&self.data)

    def release(self):
        gevent_semaphore_release(&self.data)

    property counter:

        def __get__(self):
            return self.data.counter

        def __set__(self, int counter):
            self.data.counter = counter


def spawn(*args, **kwargs):
    cdef cothread t = cothread(*args, **kwargs)
    t.spawn()
    return t


def sleep(timeout):
    cdef long c_timeout = timeout * 1000.0
    if gevent_sleep(gevent_default_hub(), c_timeout):
        raise RuntimeError


def wait(timeout=None):
    cdef long c_timeout
    if timeout is None:
        c_timeout = 0
    else:
        c_timeout = timeout
    if gevent_wait(gevent_default_hub(), c_timeout):
        raise MemoryError


def getaddrinfo(*args):
    # XXX useless wrapper
    return pygevent_getaddrinfo(args)
