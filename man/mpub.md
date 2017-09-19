mpub(8) -- multicast heartbeat publisher
========================================

## SYNOPSIS
`mpub` [OPTIONS] _iface_=_maddr_ [iface=maddr ...]

## DESCRIPTION
The `mpub` utility publishes multicast UDP datagrams to a set of network
endpoints.  It can be used in conjunction with the `msub(8)` utility to
test and debug multicast network configurations.

## OPTIONS
The utility accepts the following command-line options:

  * `-b` _BSZ_:
    Sets the socket send buffer size to _BSZ_ bytes. If not specified,
    the value defaults to the Linux kernel default.

  * `-c` _CNT_:
    Sets the number of outgoing datagrams per endpoint to _CNT_. If
    not specified, the value defaults to 5.

  * `-e`:
    The process will terminate when the first publishing error is encountered.
    If not specified, the process will only print the relevant error message.

  * `-h`:
    Prints the usage message.

  * `-i` _DUR_:
    Sets the interval between outgoing datagrams per endpoint to
    the specified duration (see DURATION FORMAT). If not specified, the
    interval defaults to 1 second.

  * `-l`:
    Enables local loopback for published multicast datagrams.

  * `-p` _NUM_:
    Specify the UDP port of all created endpoints. The default value is 22999.

  * `-s` _SID_:
    Sets the session ID of each outgoing mbeat payload to _SID_ (see
    SESSION IDENTIFICATION).  If not specified, a random value is generated.

  * `-t` _TTL_:
    Sets the Time-To-Live property of each outgoing datagram to
    _TTL_. If not specified, the value defaults to 64.

## ENDPOINTS
The positional arguments of the utility are endpoints: an ordered tuple
consisting of local interface name, multicast group and the multicast port. It
is possible to specify up to 83886080 endpoints.

## SESSION IDENTIFICATION
In order to support multiple simultaneous runs of the tool, the publisher can
stamp the payload with a session ID - a 64-bit unsigned integer - that
identifies the set of outgoing packets. Similarly the subscriber utility is
able to filter out everything but a given session ID.

## DURATION FORMAT
The time duration has to be specified by an unsigned integer, followed by a
time unit. An example of a valid duration is _1s_. Supported units are: _ns_,
_us_, _ms_, _s, _m, _h, _d_. Zero duration is allowed, e.g. _0ms_.

## PAYLOAD FORMAT
The format of the payload is binary. All numeric fields are unsigned
integers in network byte order, while the 64-bit numbers are split into high
and low 32-bits, encoded in the network byte order. The total payload size is
128 bytes. All valid payloads must start with a magic number 0x6d626974, which
stands for four ASCII letters "mbit". The current format version is 2.

Each payload contains the following fields in order:

 * magic value (4 bytes)
 * format version number (1 byte)
 * source Time-To-Live value (1 byte)
 * multicast port (2 bytes)
 * multicast group (4 bytes)
 * time of departure nanoseconds part (4 bytes)
 * time of departure UNIX timestamp (8 bytes)
 * session ID (8 bytes)
 * sequence interation counter (8 bytes)
 * sequence length (8 bytes)
 * publisher's interface name (16 bytes)
 * publisher's hostname (64 bytes)

## RETURN VALUE
The process returns _0_ on success, _1_ on failure. Warnings and errors are
printed to the standard error stream.

## SEE ALSO
msub(8)
