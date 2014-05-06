### green threading library for C on top of libuv and PyPy's stacklet

This is just an experiment, only useful to benchmark things. See [gevent.h](gevent.h) for documentation.

### Python wrappers

For documentation on Python API see examples and tests in [python/](python/).

The channel performance (if it's indication of anything) is pretty good, only half as slow as Stackless (which uses soft-switching there I believe).

```
Testing pypy: 2.7.3 (2.2.1+dfsg-1~ppa1, Nov 28 2013, 02:02:56) [PyPy 2.2.1 with GCC 4.6.3]
+ pypy channel_comparison.py test_pypy
<module 'stackless' from '/usr/lib/pypy/lib_pypy/stackless.py'>
1000000 sends took 0.397s

Testing stackless: 2.7.6 Stackless 3.1b3 060516 (v2.7.6-slp:2f45143b8ccb, Apr 30 2014, 23:14:55)  [GCC 4.6.3]
+ stackless channel_comparison.py test_stackless
<module 'stackless' (built-in)>
1000000 sends took 0.283s

Testing /usr/bin/python: 2.7.3 (default, Feb 27 2014, 19:58:35)  [GCC 4.6.3]
+ /usr/bin/python channel_comparison.py test_gevent2
<module 'gevent2' from '/home/denis/work/libgevent/python/gevent2.so'>
1000000 sends took 0.503s

Testing /usr/bin/python: 2.7.3 (default, Feb 27 2014, 19:58:35)  [GCC 4.6.3]
+ /usr/bin/python channel_comparison.py test_gevent1
<module 'gevent' from '/usr/lib/pymodules/python2.7/gevent/__init__.pyc'> 1.0.1
1000000 sends took 17.720s
```
