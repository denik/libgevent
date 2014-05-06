import gevent
from gevent.queue import Channel as channel
from time import time

ch = channel()
N = 1000 * 1000
count = N

def func():
    global count
    while count > 0:
        ch.put(ch.get())
        count -= 1


start = time()
gevent.spawn(func)
ch.put(True)
gevent.spawn(func)
gevent.wait()
took = time() - start
assert not count, count
print '%s sends took %.3fs: %.3fns' % (N, took, took * 1000. * 1000. / N)
