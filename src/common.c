/*
 *  Copyright (c) 2017 Two Sigma Open Source, LLC.
 *  All Rights Reserved
 *
 *  Distributed under the terms of the 2-clause BSD License. The full
 *  license is in the file LICENSE, distributed as part of this software.
**/

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
#include <limits.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <err.h>

#include "common.h"


/** Convert a string into an unsigned 32-bit integer.
 *
 * @param[out] out resulting integer
 * @param[in]  str string
 * @param[in]  min minimal allowed value (inclusive)
 * @param[in]  max maximal allowed value (inclusive)
 * 
 * @return status code
**/
bool
parse_uint32(uint32_t* out,
             const char* str,
             const uint32_t min,
             const uint32_t max)
{
  uintmax_t x;

  errno = 0;
  x = strtoumax(str, NULL, 10);
  if (x == 0 && errno != 0) {
    warn("Unable to parse a number from string '%s'", str);
    return false;
  }

  if (x < (uintmax_t)min || x > (uintmax_t)max) {
    warnx("Number %ju out of range (%u..%u)", x, min, max);
    return false;
  }

  *out = (uint32_t)x;
  return true;
}

/** Return the lesser of two unsigned integers.
 * 
 * @param[in] a first number
 * @param[in] b second number
 *
 * @return lesser of the two numbers
**/
static size_t
lesser(const size_t a, const size_t b)
{
  return a < b ? a : b;
}

/** Parse and validate the interface.
 *
 * @param[out] ep  connection endpoint
 * @param[in]  inp input string
 * @param[in]  ifs list of interfaces
 *
 * @return status code
**/
static bool 
parse_iface(endpoint* ep, const char* inp, const struct ifaddrs* ifaces)
{
  const struct ifaddrs* iface;
  struct sockaddr_in* if_addr_in;

  /* Find a suitable interface. */
  for (iface = ifaces; iface != NULL; iface = iface->ifa_next) {
    if (iface->ifa_addr == NULL)
      continue;

    /* Skip non-IPv4 interfaces. */
    if (iface->ifa_addr->sa_family != AF_INET)
      continue;

    if (strcmp(inp, iface->ifa_name) == 0)
      break;
  }

  /* If no suitable interface was found. */
  if (iface == NULL) {
    warnx("Unable to find interface '%s' with an IPv4 address", inp);
    return false;
  }

  /* Make sure that the interface is up. */
  if (!(iface->ifa_flags & IFF_UP)) {
    warnx("Interface '%s' is not up", inp);
    return false;
  }

  /* Make sure that the interface supports multicast traffic. */
  if (!(iface->ifa_flags & IFF_MULTICAST)) {
    warnx("Interface '%s' is not available for multicast traffic", inp);
    return false;
  }

  /* Assign the found interface address to the endpoint array. */
  if_addr_in = (struct sockaddr_in*)iface->ifa_addr;
  ep->ep_iaddr = if_addr_in->sin_addr;

  /* Copy the interface name to the endpoint. */
  memcpy(ep->ep_iname, inp, lesser(INAME_LEN, strlen(inp)));
  
  return true;
}

/** Parse and validate the multicast address.
 *
 * @param[out] ep  connection endpoint
 * @param[in]  inp input string
 *
 * @return status code
**/
static bool 
parse_maddr(endpoint* ep, const char* inp)
{
  /* Convert and validate the multicast address. */
  if (inet_aton(inp, &ep->ep_maddr) == 0) {
    warnx("Unable to parse the multicast address '%s'", inp);
    return false;
  }

  /* Ensure that the address belongs to the multicast range. */
  if (!IN_MULTICAST(ntohl(ep->ep_maddr.s_addr))) {
    warnx("Address '%s' does not belong to the multicast range", inp);
    return false;
  }

  return true;
}

/** Parse all endpoints from the command-line argument vector.
 *
 * @param[out] eps    endpoint array
 * @param[in]  ep_idx index into argv where the endpoints start
 * @param[in]  argv   argument vector
 * @param[in]  ep_cnt number of endpoint entries
 *
 * @return status code
**/
bool
parse_endpoints(endpoint* eps, const int ep_idx, char* argv[], const int ep_cnt)
{
  int i;
  int parts;
  bool result;
  char iname[256];
  char maddr[256];
  struct ifaddrs* ifaces;

  result = true;

  /* Populate the list of all network interfaces. */
  errno = 0;
  if (getifaddrs(&ifaces) == -1) {
    warn("Unable to populate the list of network interfaces");
    return false;
  }

  for (i = 0; i < ep_cnt; i++) {
    /* Reset all buffers. */
    memset(iname, '\0', 256);
    memset(maddr, '\0', 256);

    /* Parse the endpoint format. */
    parts = sscanf(argv[ep_idx + i], "%255[^=]=%255[^:]", iname, maddr);
    if (parts != 2) {
      warnx("Unable to parse the endpoint '%s'", argv[ep_idx + i]);
      result = false;
      break;
    }

    /* Parse all endpoint parts. */
    memset(&eps[i], 0, sizeof(eps[i]));

    if (!parse_iface(&eps[i], iname, ifaces)) {
      result = false;
      break;
    }

    if (!parse_maddr(&eps[i], maddr)) {
      result = false;
      break;
    }
  }

  /* Release resources held by the interface list. */
  freeifaddrs(ifaces);

  return result;
}

/** Allocate memory for endpoint storage.
 * 
 * @param[out] eps    endpoint array
 * @param[in]  ep_cnt number of endpoints
 *
 * @return status code
**/
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

/** Obtain the hostname.
 *
 * @param[out] hname     hostname
 * @param[in]  hname_len maximal hostname length
 *
 * @return status code
**/
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

/** Convert time in milliseconds into seconds and nanoseconds.
 *
 * @param[out] tv seconds and nanoseconds
 * @param[in]  ms milliseconds
**/
void
convert_millis(struct timespec* tv, const uint32_t ms)
{
  tv->tv_sec = ms / 1000;
  tv->tv_nsec = (ms % 1000) * 1000000;
}
