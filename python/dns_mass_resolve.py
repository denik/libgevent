#!/usr/bin/python
"""Resolve hostnames concurrently, exit after 2 seconds.

This is ported from gevent examples, except it does not use a pool, since there isn't one for gevent2 yet.
"""
from __future__ import with_statement
import sys
import gevent2 as gevent
import traceback
from time import time


N = 100
finished = 0


def job(domain):
    print 'starting', domain
    global finished
    try:
        try:
            ip = gevent.getaddrinfo(domain, 'http')
            print ('%s = %s' % (domain, ip))
        #except socket.gaierror:
            #ex = sys.exc_info()[1]
            #print ('%s failed with %s' % (domain, ex))
        except:
            pass
            #traceback.print_exc()
    finally:
        finished += 1


start = time()

for x in xrange(10, 10 + N):
    gevent.spawn(job, '%s.com' % x)

print 'Spawning took %.2f (%r/%r finished)' % (time() - start, finished, N)

gevent.wait()

print ('finished after 2 seconds: %s/%s' % (finished, N))
