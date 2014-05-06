import stackless
from time import time

N = 1000 * 1000
count = N


def func(ch):
    global count
    while count > 0:
        ch.send(ch.receive())
        count -= 1


ch = stackless.channel()
start = time()
stackless.tasklet(func)(ch)
ch.send(True)
stackless.tasklet(func)(ch)
stackless.run()
took = time() - start
assert not count, count
print '%s send took %.3fs' % (N, took)
