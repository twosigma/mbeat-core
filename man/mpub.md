mbeat_pub(8) -- multicast heartbeat publisher
=============================================

## SYNOPSIS
`mbeat_pub` [`-b` _BSZ_] [`-c` _CNT_] [`-h`] [`-i` _MS_] [`-l`] [`-s` _SID_]
[`-t` _TTL_] _iface_=_maddr_:_mport_ [iface=maddr:mport ...]

## DESCRIPTION
The `mbeat_pub` utility publishes multicast UDP datagrams to a set of network
endpoints.  It can be used in conjunction with the `mbeat_sub(8)` utility to
test and debug network configurations.

## OPTIONS
The utility accepts the following command-line options:

  * `-b` _BSZ_:
    Sets the socket send buffer size to _BSZ_ bytes. If not specified,
    the value defaults to the Linux kernel default.

  * `-c` _CNT_:
    Sets the number of outgoing datagrams per endpoint to _CNT_. If
    not specified, the value defaults to 5.

  * `-h`:
    Prints the usage message.

  * `-i` _MS_:
    Sets the interval between outgoing datagrams per endpoint to
    _MS_ milliseconds. If not specified, the value defaults to 1000.

  * `-l`:
    Enables local loopback for published multicast datagrams.

  * `-s` _SID_:
    Sets the session ID of each outgoing mbeat payload to _SID_ (see
    SESSION IDENTIFICATION).  If not specified, a random value is generated.

  * `-t` _TTL_:
    Sets the Time-To-Live property of each outgoing datagram to
    _TTL_. If not specified, the value defaults to 64.

## ENDPOINTS
The positional arguments of the utility are endpoints: an ordered tuple
consisting of local interface name, multicast group and the multicast port. It
is possible to specify up to 2048 endpoints per publisher.

## SESSION IDENTIFICATION
In order to support multiple simultaneous runs of the tool, the publisher can
stamp the payload with a session ID - a 32-bit unsigned integer - that
identifies the set of outgoing packets. Similarly the subscriber utility is
able to filter out everything but a given session ID.

## PAYLOAD FORMAT
The format of the payload is binary. All numeric fields are unsigned
integers in network byte order. The total payload size is 108 bytes.

Each payload contains the following fields in order:

 * format version number (2 bytes)
 * multicast port (2 bytes)
 * multicast group (4 bytes)
 * sequence number (4 bytes)
 * session ID (4 bytes)
 * local interface name (16 bytes)
 * local host name (64 bytes)
 * time of departure UNIX timestamp (8 bytes)
 * time of departure nanoseconds part (4 bytes)

## RETURN VALUE
The process returns _0_ on success, _1_ on failure.
Warnings and errors are printed to standard error.

## SEE ALSO
mbeat_sub(8)
