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


/// Allocate memory for endpoint storage.
/// @return status code
/// 
/// @param[out] eps    endpoint array
/// @param[in]  ep_cnt number of endpoints
bool
allocate_endpoints(endpoint** eps, const int ep_cnt)
{
  if (ep_cnt < 1) {
    warnx("Expected at least one endpoint");
    return false;
  }

  if (ep_cnt > ENDPOINT_MAX) {
    warnx("Too many endpoints, maximum is %d", ENDPOINT_MAX);
    return false;
  }

  *eps = calloc(ep_cnt, sizeof(**eps));
  if (*eps == NULL) {
    warn("Unable to allocate memory for endpoints");
    return false;
  }

  return true;
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
