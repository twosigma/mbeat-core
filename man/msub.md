mbeat_sub(8) -- multicast heartbeat subscriber 
==============================================

## SYNOPSIS
`mbeat_sub` [`-b` _BSZ_] [`-e` _CNT_] [`-h`] [`-o` _OFF_] [`-r`] [`-s` _SID_]
[`-t` _MS_] [`-u`] _iface_=_maddr_:_mport_ [iface=maddr:mport ...]

## DESCRIPTION
The `mbeat_sub` utility subscribes to multicast UDP datagrams for a set of
network endpoints.  It can be used in conjunction with the `mbeat_pub(8)`
utility to test and debug network configurations.

## OPTIONS
The utility accepts the following command-line options:

  * `-b` _BSZ_:
    Sets the socket receive buffer size to _BSZ_ bytes. If not specified,
    the value defaults to the Linux kernel default.

  * `-e` _CNT_:
    If set, specifies the total number of datagrams to receive after which
    the process will exit with success.

  * `-h`:
    Prints the usage message.

  * `-o` _OFF_:
    Sets the sequence number offset, so that all mbeat payloads with sequence
    number lesser than _OFF_ are ignored. All subsequent sequence numbers are
    printed shifted by the _OFF_ value.

  * `-r`:
    Enables the raw binary output instead of the default CSV.
    See `OUTPUT FORMAT`.

  * `-s` _SID_:
    Sets the session ID filter on incoming mbeat payloads, so that only those
    with session ID equal to _SID_ get printed out
    (see SESSION IDENTIFICATION). If not specified, all payloads are accepted.

  * `-t` _MS_:
    If set, the process will timeout and exit after _MS_ milliseconds.

  * `-u`:
    Disables output buffering.

## ENDPOINTS
The positional arguments of the utility are endpoints: an ordered tuple
consisting of local interface name, multicast group and the multicast port. It
is possible to specify up to 2048 endpoints per subscriber.

## SESSION IDENTIFICATION
In order to support multiple simultaneous runs of the tool, the publisher can
stamp the payload with a session ID - a 32-bit unsigned integer - that
identifies the set of outgoing packets. In return, the subscriber utility is
able to exclusively listen to only certain session ID.

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

## OUTPUT FORMAT - CSV
The default output format is ASCII-encoded CSV file complaint with the RFC4180
standard. The table has the following column headers:

 * SequenceNum
 * SessionID
 * MulticastAddr
 * MulticastPort
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

Unlike the CSV format, there is no header entry in raw binary. Similar to the
payload representation, data is outputted in the big-endian byte order.

## RETURN VALUE
The process returns _0_ on success, _1_ on failure.
Normal program output is printed on standard output and warnings and errors
on standard error.

## SEE ALSO
mbeat_pub(8)
