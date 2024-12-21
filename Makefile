
LDLIBS += -lpthread
CFLAGS += -g3 -O0 -MMD

all: pty-demo expect-demo

pty-demo: tlpi-pt.o phil-expect.o pty-demo.o

test: pty-demo
	./$< X X bash

expect-demo: expect-demo.o tlpi-pt.o phil-expect.o

t2: expect-demo
	./$< X X bash

clean:
	rm -f *.o *.d main

-include *.d
