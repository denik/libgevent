all: test test_sizeof test_getaddrinfo test_getaddrinfo_uv test_channel

test: test.c Makefile gevent.c gevent.h stacklet/*
	gcc -Wall -g test.c gevent.c -I libuv/include/ -L libuv/ -luv -lpthread  -lm -lrt -ldl -o test

test_sizeof: test_sizeof.c Makefile gevent.c gevent.h stacklet/*
	gcc -Wall -g test_sizeof.c gevent.c -I libuv/include/ -L libuv/ -luv -lpthread  -lm -lrt -ldl -o test_sizeof

test_getaddrinfo: test_getaddrinfo.c Makefile gevent.c gevent.h stacklet/*
	gcc -Wall -g test_getaddrinfo.c gevent.c -I libuv/include/ -L libuv/ -luv -lpthread  -lm -lrt -ldl -o test_getaddrinfo

test_channel: test_channel.c Makefile gevent.c gevent.h stacklet/*
	gcc -Wall -g test_channel.c gevent.c -I libuv/include/ -L libuv/ -luv -lpthread  -lm -lrt -ldl -o test_channel

test_getaddrinfo_uv: test_getaddrinfo_uv.c Makefile gevent.c gevent.h stacklet/*
	gcc -Wall -g test_getaddrinfo_uv.c gevent.c -I libuv/include/ -L libuv/ -luv -lpthread  -lm -lrt -ldl -o test_getaddrinfo_uv

clean:
	rm -f test test_sizeof test_getaddrinfo test_getaddrinfo_uv test_channel
