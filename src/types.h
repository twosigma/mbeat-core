// Copyright (c) 2017-2018 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#ifndef MBEAT_TYPES_H
#define MBEAT_TYPES_H

#include <stdlib.h>
#include <netinet/in.h>


// Limitations for payload data size.
#define INAME_LEN 16 // Maximal interface name length.
#define HNAME_LEN 64 // Maximal hostname length.

/// Payload of the datagram (136 bytes).
typedef struct _payload {
  uint32_t pl_magic;            ///< Magic identifier.
  uint8_t  pl_fver;             ///< Format version.
  uint8_t  pl_ttl;              ///< Source Time-To-Live.
  uint16_t pl_mport;            ///< Multicast IPv4 port.
  uint32_t pl_maddr;            ///< Multicast IPv4 address.
  uint32_t pl_pad;              ///< Padding (unused).
  uint64_t pl_rsec;             ///< System time of departure (ns).
  uint64_t pl_msec;             ///< Steady time of departure (ns).
  uint64_t pl_key;              ///< Unique key.
  uint64_t pl_snum;             ///< Sequence iteration counter.
  uint64_t pl_slen;             ///< Sequence length.
  char     pl_iname[INAME_LEN]; ///< Publisher's interface name.
  char     pl_hname[HNAME_LEN]; ///< Publisher's hostname.
} payload;

/// Raw binary output format (228 bytes).
typedef struct _raw_output {
  payload  ro_pl;               ///< Received payload.
  char     ro_iname[INAME_LEN]; ///< Subscriber's interface name.
  char     ro_hname[HNAME_LEN]; ///< Subscriber's hostname.
  uint64_t ro_rsec;             ///< System time of arrival (ns).
  uint64_t ro_msec;             ///< Steady time of arrival (ns).
  uint8_t  ro_ttla;             ///< Availability of the Time-To-Live value.
  uint8_t  ro_ttl;              ///< Destination Time-To-Live value.
  uint8_t  ro_pad[2];           ///< Padding (unused).
} raw_output;

/// Connection between a local interface and a multicast group.
typedef struct _endpoint {
  int               ep_sock;             ///< Connection socket.
  struct in_addr    ep_maddr;            ///< Multicast address.
  struct in_addr    ep_iaddr;            ///< Local interface address.
  char              ep_iname[INAME_LEN]; ///< Local interface name.
  struct _endpoint* ep_next;             ///< Link to the next endpoint.
} endpoint;

#endif
