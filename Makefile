all: gevent.a

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

ifdef MSVC
uname_S := MINGW
endif

CPPFLAGS += -Iinclude -Ilibuv/include -Ilibuv/include/uv-private -I.

ifeq (Darwin,$(uname_S))
SOEXT = dylib
else
SOEXT = so
endif

ifneq (,$(findstring MINGW,$(uname_S)))
include libuv/config-mingw.mk
else
include libuv/config-unix.mk
endif

libuv/libuv.a:
	cd libuv && make libuv.a

libuv/libuv.so:
	cd libuv && make libuv.so

src/%.o: src/%.c
	# include/*.h include/uv-private/*.h include/*.h stacklet/*.*
	$(CC) $(CSTDFLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

gevent.a: src/gevent.o libuv/libuv.a
	$(AR) rcs $@ $^

gevent.$(SOEXT): CFLAGS += -fPIC
gevent.$(SOEXT): src/gevent.o
	$(CC) -shared -o $@ $^ $(LINKFLAGS)

HELPERS=libuv/test/test-stdio-over-pipes.c libuv/test/test-ipc.c libuv/test/test-ipc-send-recv.c libuv/test/test-platform-output.c
TESTS=libuv/test/blackhole-server.c libuv/test/echo-server.c test/test-*.c $(HELPERS)
BENCHMARKS=libuv/test/blackhole-server.c libuv/test/echo-server.c test/benchmark-*.c $(HELPERS)

test/run-tests$(E): CPPFLAGS += -Ilibuv/test
test/run-tests$(E): libuv/test/run-tests.c libuv/test/runner.c libuv/$(RUNNER_SRC) $(TESTS) libuv/libuv.$(SOEXT) gevent.$(SOEXT)
	mv -f libuv/test/test-list.h libuv/test/test-list.h.saved 2> /dev/null || true
	$(CC) $(CPPFLAGS) $(RUNNER_CFLAGS) -o $@ $^ -Llibuv $(RUNNER_LIBS) $(RUNNER_LINKFLAGS)

test/run-benchmarks$(E): CPPFLAGS += -Ilibuv/test
test/run-benchmarks$(E): libuv/test/run-benchmarks.c libuv/test/runner.c libuv/$(RUNNER_SRC) $(BENCHMARKS) libuv/libuv.$(SOEXT) gevent.$(SOEXT)
	mv -f libuv/test/benchmark-list.h libuv/test/benchmark-list.h.saved 2> /dev/null || true
	$(CC) $(CPPFLAGS) $(RUNNER_CFLAGS) -o $@ $^ -Llibuv $(RUNNER_LIBS) $(RUNNER_LINKFLAGS)

#test/echo.o: test/echo.c test/echo.h


.PHONY: clean clean-platform distclean distclean-platform test bench


test: test/run-tests$(E)
	$<

bench: test/run-benchmarks$(E)
	$<

clean:
	$(RM) src/*.o *.a test/run-tests$(E) test/run-benchmarks$(E)
	cd libuv && make clean
