
LDLIBS += -lpthread
CFLAGS += -g3 -O0 -MMD

main: tlpi-pt.o phil-expect.o main.o

clean:
	rm -f *.o *.d main
-include *.d
