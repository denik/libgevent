CYTHON ?= cython

all: gevent2.c

gevent2.c: gevent2.pyx
	$(CYTHON) -o $@ $<
	echo                          >> $@
	echo '#include "callbacks.c"' >> $@

gevent2.so: gevent2.c
	python setup.py build

clean:
	rm -fr gevent2.c gevent2.so

test: gevent2.so
	python test_spawn.py
	python test_channel.py

.PHONY: clean all test
