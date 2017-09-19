// Copyright (c) 2017 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include "common.h"


/// Free memory used for endpoint storage.
/// 
/// @param[in] eps endpoint list
void
free_endpoints(endpoint* eps)
{
  endpoint* ep;
  endpoint* tofree;

  ep = eps;
  tofree = NULL;

  // Traverse all endpoints in the list and free each one.
  while (ep != NULL) {
    tofree = ep;
    ep = ep->ep_next;
    free(tofree);
  }
}

/// Obtain the hostname.
/// @return status code
///
/// @param[out] hname     hostname
/// @param[in]  hname_len maximal hostname length
bool
cache_hostname(char* hname, const size_t hname_len)
{
  if (hname_len == 0) {
    warnx("Hostname length cannot be zero");
    return false;
  }

  memset(hname, '\0', hname_len);

  if (gethostname(hname, hname_len-1) == -1) {
    if (errno == ENAMETOOLONG)
      warnx("Hostname was truncated to %zu characters", hname_len-1);
    else {
      warn("Unable to get the local hostname");
      return false;
    }
  }

  return true;
}

/// Convert time in only nanoseconds into seconds and nanoseconds.
///
/// @param[out] tv seconds and nanoseconds
/// @param[in]  ns nanoseconds
void
convert_nanos(struct timespec* tv, const uint64_t ns)
{
  tv->tv_sec = ns / 1000000000;
  tv->tv_nsec = (ns % 1000000000);
}

/// Encode a 64-bit unsigned integer for a reliable network transmission.
/// @return encoded integer
///
/// @param[in] x integer
uint64_t
htonll(const uint64_t x)
{
  uint32_t hi;
  uint32_t lo;

  hi = x >> 32;
  lo = x & 0xffffffff;

  return (uint64_t)htonl(lo) | ((uint64_t)htonl(hi) << 32);
}

/// Decode a 64-bit unsigned integer that was transmitted over a network.
/// @return decoded integer
///
/// @param[in] x integer
uint64_t
ntohll(const uint64_t x)
{
  uint32_t hi;
  uint32_t lo;

  hi = x >> 32;
  lo = x & 0xffffffff;

  return (uint64_t)ntohl(lo) | ((uint64_t)ntohl(hi) << 32);
}
