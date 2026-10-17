
LDLIBS += -lpthread
CFLAGS += -g3 -O0 -MMD

all: standalone-demo unbuffer pty-demo expect-demo

unbuffer: tlpi-pt.o unbuffer.o

pty-demo: tlpi-pt.o phil-expect.o pty-demo.o

test: standalone-demo
	./standalone-demo X $$'git comm\t --am\t --no\ted\t' bash

expect-demo: expect-demo.o tlpi-pt.o phil-expect.o

t2: expect-demo
	./$< X X bash

clean:
	rm -f *.o *.d main pty-demo expect-demo standalone-demo

-include *.d
