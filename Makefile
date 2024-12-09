
LDLIBS += -lpthread
CFLAGS += -g3 -O0 -MMD

main: tlpi-pt.o phil-expect.o main.o

test: main
	./$< X X bash

clean:
	rm -f *.o *.d main
-include *.d
