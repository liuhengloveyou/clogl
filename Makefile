AR=ar
CC=gcc
CFLAGS=-std=gnu99 -O2 -Wall -Wextra -fPIC
INCLUDE=-I.

incs = clogl.h
libs = libclogl.a
objs = ./clogl.o

all: lib

clean:
	rm -rf $(libs) $(objs)

lib: $(libs)

####################################################
$(libs): %: $(objs)
	$(AR) -rs $@ $^

$(objs): %.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $< $(INCLUDE)
