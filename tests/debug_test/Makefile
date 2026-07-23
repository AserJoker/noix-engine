CC = gcc
CFLAGS = -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
         -D_GNU_SOURCE -D_CRT_SECURE_NO_WARNINGS
LDFLAGS = -lm -ldl -lpthread

QUICKJS_SRC = ../../third_party/quickjs
QUICKJS_SRCS = $(QUICKJS_SRC)/quickjs.c $(QUICKJS_SRC)/quickjs-libc.c

all: test_debug

quickjs.o: $(QUICKJS_SRC)/quickjs.c $(QUICKJS_SRC)/quickjs.h $(QUICKJS_SRC)/quickjs-opcode.h
	$(CC) $(CFLAGS) -I$(QUICKJS_SRC) -c -o $@ $<

quickjs-libc.o: $(QUICKJS_SRC)/quickjs-libc.c
	$(CC) $(CFLAGS) -I$(QUICKJS_SRC) -c -o $@ $<

test_debug: test_debug.c quickjs.o quickjs-libc.o
	$(CC) $(CFLAGS) -I$(QUICKJS_SRC) -o $@ $^ $(LDFLAGS)

valgrind: test_debug
	valgrind --leak-check=full --error-exitcode=1 ./test_debug

gdb: test_debug
	gdb ./test_debug

clean:
	rm -f test_debug *.o

.PHONY: all valgrind gdb clean
