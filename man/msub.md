msub(8) -- multicast heartbeat subscriber
=========================================

## SYNOPSIS
`msub` [OPTIONS] _iface_=_maddr_ [iface=maddr ...]

## DESCRIPTION
The `msub` utility subscribes to multicast UDP datagrams for a set of
network endpoints.  It can be used in conjunction with the `mpub(8)`
utility to test and debug multicast network configurations.

## OPTIONS
The utility accepts the following command-line options:

  * `-b` _BSZ_:
    Sets the socket receive buffer size to _BSZ_ bytes. If not specified,
    the value defaults to the Linux kernel default.

  * `-e`:
    The process will terminate when the first receiving error is encountered.
    If not specified, the process will only print the relevant error message.

  * `-h`:
    Prints the usage message.

  * `-o` _OFF_:
    Sets the sequence number offset, so that all mbeat payloads with sequence
    number lesser than _OFF_ are ignored. All subsequent sequence numbers are
    printed shifted by the _OFF_ value.

  * `-p` _NUM_:
    Specify the UDP port of all created endpoints. The default value is 22999.

  * `-r`:
    Enables the raw binary output instead of the default CSV.
    See `OUTPUT FORMAT`.

  * `-s` _SID_:
    Sets the session ID filter on incoming mbeat payloads, so that only those
    with session ID equal to _SID_ get printed out
    (see SESSION IDENTIFICATION). If not specified, all payloads are accepted.

  * `-t` _DUR_:
    If set, the process will terminate after the selected time duration passes.

  * `-u`:
    Disables output buffering.

  * `-v`:
    Enables more verbose logging. Repeating this flag will turn on more
    detailed levels of logging messages: INFO, DEBUG, and TRACE.

## ENDPOINTS
The positional arguments of the utility are endpoints: an ordered tuple
consisting of local interface name, multicast group and the multicast port. It
is possible to specify up to 83886080 endpoints.

## SESSION IDENTIFICATION
In order to support multiple simultaneous runs of the tool, the publisher can
stamp the payload with a session ID - a 64-bit unsigned integer - that
identifies the set of outgoing packets. In return, the subscriber utility is
able to exclusively listen to only certain session ID.

## DURATION FORMAT
The time duration has to be specified by an unsigned integer, followed by a
time unit. An example of a valid duration is _1s_. Supported units are: _ns_,
_us_, _ms_, _s_, _m_, _h_, _d_. Zero duration is allowed, e.g. _0ms_.

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

## OUTPUT FORMAT - CSV
The default output format is ASCII-encoded CSV file complaint with the RFC4180
standard. The table has the following column headers (listed in order):

 * SessionID
 * SequenceNum
 * SequenceLen
 * MulticastAddr
 * MulticastPort
 * SrcTTL
 * DstTTL
 * PubInterface
 * PubHostname
 * SubInterface
 * SubHostname
 * TimeOfDeparture
 * TimeOfArrival

## OUTPUT FORMAT - RAW BINARY
The raw binary format re-uses the exact structure of the payload, while
appending 4 more fields:

 * interface name on the receivers end (16 bytes)
 * host name on the receivers end (64 bytes)
 * time of arrival UNIX timestamp (8 bytes)
 * time of arrival nanoseconds part (4 bytes)
 * destination Time-To-Live value availability (1 byte)
 * destination Time-To-Live value (1 byte)
 * padding - unused (2 bytes)

Unlike the CSV format, there is no header entry in raw binary. Unlike the
on-wire payload representation, data is outputted in the host byte order.

## RETURN VALUE
The process returns _0_ on success, _1_ on failure.
Normal program output is printed on the standard output stream, while warnings
and errors appear on the standard error stream.

## SEE ALSO
mpub(8)
