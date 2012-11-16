test: test.c Makefile gevent.c
	gcc -g test.c gevent.c -I libuv/include/ -L libuv/ -luv -lpthread  -lm -lrt -ldl -o test

clean:
	rm -f test
