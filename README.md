# mbeat-core

[![Build Status](https://travis-ci.org/twosigma/mbeat-core.svg?branch=master)](https://travis-ci.org/twosigma/mbeat-core)

Multicast heartbeat - `mbeat` - is a set of command-line utilities perform
testing and debugging multicast network configurations by transmitting
diagnostic payloads and reporting the perceived state. The project
consists of two components: the publisher and the subscriber. Each
component is implemented as a separate executable, named `mpub` and `msub`
respectively.

## Example
The following example runs the two programs on different hosts, `freebsd`
and `linux`, where the publishers process sends 10 datagrams, all
successfully received by the subscriber process, and the respective lines
printed to the standard output stream in the CSV format. Detailed
explanation of the command-line options and the output columns can be
found in the manual pages for each program.

### Publisher
```
dlovasko@freebsd:~$ mpub -c10 -s100 -t32 -i1s em0=239.192.40.1
```

### Subscriber
```
dlovasko@linux:~$ msub -s100 eth0=239.192.40.1
SID,SeqNum,SeqLen,McastAddr,McastPort,SrcTTL,DstTTL,PubIf,PubHost,SubIf,SubHost,TimeDep,TimeArr
100,0,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174712.965,1506174712.965
100,1,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174713.971,1506174713.971
100,2,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174714.976,1506174714.977
100,3,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174715.979,1506174715.979
100,4,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174716.990,1506174716.991
100,5,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174717.993,1506174717.993
100,6,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174719.003,1506174719.003
100,7,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174720.007,1506174720.007
100,8,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174721.017,1506174721.017
100,9,10,239.192.40.1,22999,32,31,em0,freebsd,eth0,linux,1506174722.021,1506174722.022
```

## Build
The project was designed with portability in mind - hence absolutely no
external dependencies are required. The language standard used across the
codebase is C99, while targeting the POSIX.1-2008 standard. Exceptions are
taken with respect to event-driven frameworks, such as `epoll` and
`kqueue`.

To build `mbeat` on Linux:
```
$ make CC=gcc
$ make install
```

To build `mbeat` on FreeBSD:
```
$ make CC=clang FTM= 
$ make install
```

### Supported platforms
The project aims at supporting 32-bit and 64-bit architectures, Linux and
FreeBSD operating systems and all major C compilers, e.g. `gcc` and
`clang`. If any combination of the above does not work, please feel free
to notify the project maintainers and/or submit a patch.

## Publisher
The publisher program `mpub` is responsible for sending diagnostic
payloads to a list of user-selected endpoints. Each endpoint is a tuple:
local network interface that should be used for the communication and a
multicast group. It is possible to select the number of sent payloads,
time interval between them, the initial Time-To-Live value, and unique
session ID to distinguish among multiple running `mpub` processes. The
full list of command-line options can be found in the respective manual
page.

## Subscriber
The subscriber program `msub` is responsible to receiving diagnostic
payloads from a list of user-selected endpoints. Just like in the case of
the publisher, each endpoint is represented by a pair of an interface and
a multicast group. Each received payload is printed to the standard output
stream in the form of a CSV record, allowing for further analysis of the
data. For performance reasons, the subscriber process relies on the event
queue frameworks, `epoll` on Linux and `kqueue` on FreeBSD. The full
listing of the command-line options can be found the respective manual
page.

## Documentation
The `mpub` and `msub` programs are documented via standard UNIX manual
pages, located in the `man/` directory. Both manual pages belong the
section 8 of the manual.  The `make install` command copies the manual
page files to the standard system location.

## Future work
The project currently implements the essential communication of the
components and straightforward reporting of the state. Further work might
include detailed analysis built on to of the subscriber's output,
orchestration of large-scale tests and daemonization of the components.

## License
The `mbeat-core` project is licensed under the terms of the [2-cause BSD
license](LICENSE).

## Authors & maintainers
Daniel Lovasko <dlovasko@twosigma.com>

## Acknowledgements
The project was initially developed in collaboration with Reenen Kroukamp.
