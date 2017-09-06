#  Copyright (c) 2017 Two Sigma Open Source, LLC.
#  All Rights Reserved
#
#  Distributed under the terms of the 2-clause BSD License. The full
#  license is in the file LICENSE, distributed as part of this software.

CC = gcc
FEATURE_TEST = -D_BSD_SOURCE -D_XOPEN_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c99 -Wall -Wextra $(FEATURE_TEST)
LDFLAGS = -lrt

all: bin/mbeat_pub bin/mbeat_sub

# executables
bin/mbeat_pub: obj/pub.o obj/common.o
	$(CC) $(LDFLAGS) obj/pub.o obj/common.o -o bin/mbeat_pub

bin/mbeat_sub: obj/sub.o obj/common.o
	$(CC) $(LDFLAGS) obj/sub.o obj/common.o -o bin/mbeat_sub

# object files
obj/common.o: src/common.c
	$(CC) $(CFLAGS) -c src/common.c -o obj/common.o

obj/pub.o: src/pub.c
	$(CC) $(CFLAGS) -c src/pub.c -o obj/pub.o

obj/sub.o: src/sub.c
	$(CC) $(CFLAGS) -c src/sub.c -o obj/sub.o

clean:
	rm -f bin/mbeat_pub
	rm -f bin/mbeat_sub
	rm -f obj/common.o
	rm -f obj/pub.o
	rm -f obj/sub.o
