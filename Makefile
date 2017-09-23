#  Copyright (c) 2017 Two Sigma Open Source, LLC.
#  All Rights Reserved
#
#  Distributed under the terms of the 2-clause BSD License. The full
#  license is in the file LICENSE, distributed as part of this software.

CC = gcc
FEATURE_TEST = -D_BSD_SOURCE             \
               -D_XOPEN_SOURCE           \
               -D_POSIX_C_SOURCE=200809L \
               -D_DEFAULT_SOURCE
CFLAGS = -std=c99 -Wall -Wextra -Werror $(FEATURE_TEST)
LDFLAGS = -lrt
BINDIR = /usr/bin

all: bin/mpub bin/msub

# executables
bin/mpub: obj/pub.o obj/common.o obj/parse.o
	$(CC) obj/pub.o obj/common.o obj/parse.o -o bin/mpub $(LDFLAGS)

bin/msub: obj/sub.o obj/common.o obj/parse.o
	$(CC) obj/sub.o obj/common.o obj/parse.o -o bin/msub $(LDFLAGS)

# object files
obj/common.o: src/common.c
	$(CC) $(CFLAGS) -c src/common.c -o obj/common.o

obj/parse.o: src/parse.c
	$(CC) $(CFLAGS) -c src/parse.c -o obj/parse.o

obj/pub.o: src/pub.c
	$(CC) $(CFLAGS) -c src/pub.c -o obj/pub.o

obj/sub.o: src/sub.c
	$(CC) $(CFLAGS) -c src/sub.c -o obj/sub.o

install:
	install -s -m 0755 bin/mpub $(BINDIR)/mpub
	install -s -m 0755 bin/msub $(BINDIR)/msub

clean:
	rm -f bin/mpub
	rm -f bin/msub
	rm -f obj/common.o
	rm -f obj/parse.o
	rm -f obj/pub.o
	rm -f obj/sub.o
