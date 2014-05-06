from __future__ import print_function
import time
import os


USE_GEVENT = os.environ.get('USE_GEVENT')
#NTHREADS = int(os.environ.get('NTHREADS') or 100)
COUNT = int(os.environ.get('COUNT') or 100000)



if USE_GEVENT:
    from gevent import monkey; monkey.patch_all()
    from gevent.lock import Semaphore
    from gevent import sleep
else:
    from threading import Semaphore
    from time import sleep


import sys
if sys.version_info[0] >= 3:
    import _thread as thread
    xrange = range
else:
    import thread


a_finished = Semaphore(0)
b_finished = Semaphore(0)

log = []

def func(source, dest, finished, rec=25):
    if rec > 0:
        return func(source, dest, finished, rec - 1)
    print ('%r started' % source)
    source_id = id(source)
    for _ in xrange(COUNT):
        source.acquire()
        log.append(source_id)
        #print '%r acquired' % source
        dest.release()
    #print '%r finishing' % source
    finished.release()


sem1, sem2 = Semaphore(0), Semaphore(0)

thread.start_new_thread(func, (sem1, sem2, a_finished))
thread.start_new_thread(func, (sem2, sem1, b_finished))

sleep(1)

print ('-----------------------')


timer = time.time
start = timer()
sem1.release()
a_finished.acquire()
b_finished.acquire()
result = timer() - start

per_thread = result * 1000000. / (COUNT * 2)
print ('USE_GEVENT=%s COUNT=%s result=%s per_thread=%dns' % (USE_GEVENT, COUNT, result, per_thread))
for index in xrange(len(log), 1):
    #print index
    assert log[index - 1] != log[index]
assert len(set(log)) == 2, set(log)
