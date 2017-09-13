// Copyright (c) 2017 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#ifndef MBEAT_TYPES_H
#define MBEAT_TYPES_H

#include <stdlib.h>


// Limitations for payload data size.
#define INAME_LEN 16 // Maximal interface name length.
#define HNAME_LEN 64 // Maximal hostname length.

/// Payload of the datagram (128 bytes).
typedef struct _payload {
  uint32_t pl_magic;            ///< Magic identifier.
  uint8_t  pl_fver;             ///< Format version.
  uint8_t  pl_ttl;              ///< Source Time-To-Live.
  uint16_t pl_mport;            ///< Multicast IPv4 port.
  uint32_t pl_maddr;            ///< Multicast IPv4 address.
  uint32_t pl_nsec;             ///< Time of departure - nanoseconds.
  uint64_t pl_sec;              ///< Time of departure - seconds.
  uint64_t pl_sid;              ///< Sequence ID.
  uint64_t pl_snum;             ///< Sequence iteration counter.
  uint64_t pl_slen;             ///< Sequence length.
  char     pl_iname[INAME_LEN]; ///< Publisher's interface name.
  char     pl_hname[HNAME_LEN]; ///< Publisher's hostname.
} payload;

/// Raw binary output format (220 bytes).
typedef struct _raw_output {
  payload  ro_pl;               ///< Received payload.
  char     ro_iname[INAME_LEN]; ///< Subscriber's interface name.
  char     ro_hname[HNAME_LEN]; ///< Subscriber's hostname.
  uint64_t ro_sec;              ///< Time of arrival - seconds.
  uint32_t ro_nsec;             ///< Time of arrival - nanoseconds.
} raw_output;

/// Connection between a local interface and a multicast group.
typedef struct _endpoint {
  int            ep_sock;             ///< Connection socket.
  struct in_addr ep_maddr;            ///< Multicast address.
  struct in_addr ep_iaddr;            ///< Local interface address.
  char           ep_iname[INAME_LEN]; ///< Local interface name.
} endpoint;

/// Command-line options of the publisher utility.
typedef struct _pub_options {
  uint64_t po_buf;  ///< Socket send buffer size in bytes.
  uint64_t po_cnt;  ///< Number of published datagrams.
  uint64_t po_int;  ///< Wait time between published datagrams.
  uint64_t po_ttl;  ///< Time-To-Live for published datagrams.
  uint64_t po_sid;  ///< Session ID of the current run.
  uint64_t po_port; ///< UDP port for all endpoints.
  uint8_t  po_err;  ///< Process exit on publishing error.
  uint8_t  po_lop;  ///< Datagram looping on localhost.
} pub_options;

/// Command-line options of the subscriber utility.
typedef struct _sub_options {
  uint64_t so_tout; ///< Execution timeout in milliseconds.
  uint64_t so_buf;  ///< Socket receive buffer size in bytes.
  uint64_t so_sid;  ///< Session ID filter of received datagrams.
  uint64_t so_exp;  ///< Quit after expected number of packets arrive.
  uint64_t so_off;  ///< Sequence number offset.
  uint64_t so_port; ///< UDP port for all endpoints.
  uint8_t  so_raw;  ///< Output received datagrams in raw binary format.
  uint8_t  so_unb;  ///< Turn off buffering on the output stream.
} sub_options;

#endif
