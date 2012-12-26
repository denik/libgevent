import gevent2 as gevent
from time import time

ch = gevent.channel()
N = 1000 * 1000
count = N

def func():
    global count
    while count > 0:
        ch.send(ch.receive())
        count -= 1


start = time()
gevent.spawn(func)
ch.send(True)
gevent.spawn(func)
gevent.wait()
took = time() - start
assert not count, count
print ('%s sends took %.3fs' % (N, took))
