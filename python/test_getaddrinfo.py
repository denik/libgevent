import gevent2
#from socket import getaddrinfo

def myfunc():
    print gevent2.getaddrinfo('localhost', 'http')


gevent2.spawn(myfunc)
print gevent2.spawn(gevent2.getaddrinfo, 'gevent.org', 'http')
gevent2.wait()
