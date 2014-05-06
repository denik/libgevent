"""
This compares perfomance of

 - gevent2.channel (libgevent wrapper)
 - gevent.queue.Channel (gevent 1.0)
 - stackless.channel (stackless Python)
 - stackless.channel (PyPy)

"""
import sys
from time import time


STACKLESS = 'stackless'
PYPY = 'pypy'
version_check = ''' -c 'import sys; print (str(sys.version).replace("\\n", " "))' '''
N = 1000 * 1000
count = N
CH = None


# stackless.channel/gevent2.channel use receive/send naming
def channel_recv_send():
    global count
    ch = CH
    while count > 0:
        ch.send(ch.receive())
        count -= 1


# gevent 1.0 Channel uses get/put naming
def channel_get_put():
    global count
    ch = CH
    while count > 0:
        ch.put(ch.get())
        count -= 1


def _test_stackless():
    global CH
    import stackless
    print stackless
    from stackless import tasklet, channel, run

    CH = channel()

    start = time()
    tasklet(channel_recv_send)()
    CH.send(True)
    tasklet(channel_recv_send)()
    run()
    return time() - start


test_stackless = [STACKLESS, _test_stackless]
test_pypy = [PYPY, _test_stackless]


def test_gevent2():
    global CH
    import gevent2
    print gevent2
    from gevent2 import spawn, channel, wait

    CH = channel()

    start = time()
    spawn(channel_recv_send)
    CH.send(True)
    spawn(channel_recv_send)
    wait()
    return time() - start


def test_gevent1():
    global CH
    import gevent
    print gevent, gevent.__version__
    from gevent.queue import Channel
    from gevent import spawn, wait

    CH = Channel()

    start = time()
    spawn(channel_get_put)
    CH.put(True)
    spawn(channel_get_put)
    wait()
    return time() - start


def main():
    import random, os
    checks = [x for x in globals().keys() if x.startswith('test_')]
    random.shuffle(checks)

    failed = False

    for item in checks:
        value = globals()[item]
        if isinstance(value, list):
            exe = value[0]
        else:
            exe = sys.executable
        sys.stderr.write('\nTesting %s: ' % exe)
        if os.system('%s %s' % (exe, version_check)):
            failed = True
            continue
        cmd = '%s %s %s' % (exe, __file__, item)
        sys.stderr.write('+ %s\n' % cmd)
        if os.system(cmd):
            failed = True

    if failed:
        sys.exit(1)


if __name__ == '__main__':
    if not sys.argv[1:]:
        main()
    else:
        [name] = sys.argv[1:]
        func = globals()[name]
        if isinstance(func, list):
            func = func[1]
        took = func()
        assert not count, count
        print '%s sends took %.3fs' % (N, took)

        # repeat
        count = N
        took = func()
        assert not count, count
        print '%s sends took %.3fs' % (N, took)
