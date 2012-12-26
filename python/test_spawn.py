import gevent2 as gevent

log = []

def hello1():
    log.append(0)
    gevent.sleep(0)
    log.append(2)

def hello2():
    log.append(1)
    gevent.sleep(0)
    log.append(3)

gevent.spawn(hello1)
gevent.spawn(hello2)
gevent.wait()
assert log == [0, 1, 2, 3], log
