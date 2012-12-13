CYTHON ?= cython

all: libgevent.c

libgevent.c: libgevent.pyx
	$(CYTHON) -o $@ $<
	echo                          >> $@
	echo '#include "callbacks.c"' >> $@

libgevent.so: libgevent.c
	python setup.py build

clean:
	rm -fr libgevent.c

test: libgevent.so
	python test_spawn.py
	python test_channel.py

.PHONY: clean all test
