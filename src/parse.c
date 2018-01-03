// Copyright (c) 2017-2018 Two Sigma Open Source, LLC.
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
#include <limits.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <err.h>

#include "parse.h"
#include "common.h"


/// Convert a string into an unsigned 64-bit integer.
/// @return status code
///
/// @param[out] out resulting integer
/// @param[in]  str string
/// @param[in]  min minimal allowed value (inclusive)
/// @param[in]  max maximal allowed value (inclusive)
bool
parse_uint64(uint64_t* out,
             const char* str,
             const uint64_t min,
             const uint64_t max)
{
  uintmax_t x;

  // Convert the input string into a number.
  errno = 0;
  x = strtoumax(str, NULL, 10);
  if (x == 0 && errno != 0) {
    notify(NL_ERROR, true, "Unable to parse a number from string '%s'", str);
    return false;
  }

  // Verify that the number belongs to the specified range.
  if (x < (uintmax_t)min || x > (uintmax_t)max) {
    notify(NL_ERROR, false, "Number %ju out of range "
           "(%" PRIu64 "..%" PRIu64 ")", x, min, max);
    return false;
  }

  *out = (uint64_t)x;
  return true;
}

/// Parse and validate the interface.
/// @return status code
///
/// @param[out] ep  connection endpoint
/// @param[in]  inp input string
/// @param[in]  ifs list of interfaces
static bool
parse_iface(endpoint* ep, const char* inp, const struct ifaddrs* ifaces)
{
  const struct ifaddrs* iface;
  struct sockaddr_in* if_addr_in;

  // Find a suitable interface.
  for (iface = ifaces; iface != NULL; iface = iface->ifa_next) {
    if (iface->ifa_addr == NULL)
      continue;

    // Skip non-IPv4 interfaces.
    if (iface->ifa_addr->sa_family != AF_INET)
      continue;

    // Skip loopback devices when selecting the default interface.
    if (inp == NULL && (iface->ifa_flags & IFF_LOOPBACK))
      continue;

    if (inp == NULL || strcmp(inp, iface->ifa_name) == 0)
      break;
  }

  // If no suitable interface was found.
  if (iface == NULL) {
    if (inp == NULL)
      notify(NL_ERROR, false, "Unable to find any suitable interface");
    else
      notify(NL_ERROR, false, "Unable to find interface %s with an "
             "IPv4 address", inp);

    return false;
  }

  // Make sure that the interface is up.
  if (!(iface->ifa_flags & IFF_UP)) {
    notify(NL_ERROR, false, "Interface %s is not up", iface->ifa_name);
    return false;
  }

  // Make sure that the interface supports multicast traffic.
  if (!(iface->ifa_flags & IFF_MULTICAST)) {
    notify(NL_ERROR, false, "Interface %s is not available for "
           "multicast traffic", iface->ifa_name);
    return false;
  }

  // Assign the found interface address to the endpoint array.
  if_addr_in = (struct sockaddr_in*)iface->ifa_addr;
  ep->ep_iaddr = if_addr_in->sin_addr;

  // Copy the interface name to the endpoint.
  memset(ep->ep_iname, '\0', sizeof(ep->ep_iname));
  strncpy(ep->ep_iname, iface->ifa_name, sizeof(ep->ep_iname));

  return true;
}

/// Parse and validate the multicast address.
/// @return status code
///
/// @param[out] ep  connection endpoint
/// @param[in]  inp input string
static bool
parse_maddr(endpoint* ep, const char* inp)
{
  // Convert and validate the multicast address.
  if (inet_aton(inp, &ep->ep_maddr) == 0) {
    notify(NL_ERROR, false, "Unable to parse the multicast address %s", inp);
    return false;
  }

  // Ensure that the address belongs to the multicast range.
  if (!IN_MULTICAST(ntohl(ep->ep_maddr.s_addr))) {
    notify(NL_ERROR, false, "Address %s does not belong to the "
           "multicast range", inp);
    return false;
  }

  return true;
}

/// Parse a single endpoint.
/// @return status code
///
/// @param[out] ep  endpoint
/// @param[in]  inp input string
/// @param[in]  ifs list of network interfaces
static bool
parse_endpoint(endpoint* ep, char* inp, const struct ifaddrs* ifs)
{
  char* eq;
  const char* iname;
  const char* maddr;

  // Validate the input string.
  if (inp == NULL || inp[0] == '\0') {
    notify(NL_ERROR, false, "Empty endpoint definition");
    return false;
  }

  // Split the string with the first equals sign and optionally parse
  // both parts of the endpoint.
  eq = strchr(inp, '=');
  if (eq == NULL) {
    iname = NULL;
    maddr = inp;
  } else {
    // Check whether the equals sign is the first character of the string.
    if (eq == inp) {
      notify(NL_ERROR, false, "Empty interface is invalid");
      return false;
    }

    *eq = '\0';
    iname = inp;
    maddr = eq + 1;
  }

  // Parse the endpoint interface.
  if (!parse_iface(ep, iname, ifs))
    return false;

  // Parse the endpoint multicast address.
  if (!parse_maddr(ep, maddr))
    return false;

  return true;
}

/// Parse all endpoints from the command-line argument vector.
/// @return status code
///
/// @param[out] eps    endpoint array
/// @param[in]  ep_idx index into argv where the endpoints start
/// @param[in]  argv   argument vector
/// @param[in]  ep_cnt number of endpoint entries
bool
parse_endpoints(endpoint** eps,
                const int ep_idx,
                char* argv[],
                const int ep_cnt)
{
  int i;
  bool result;
  struct ifaddrs* ifaces;
  endpoint* new;

  if (ep_cnt < 1) {
    notify(NL_ERROR, false, "Expected at least one endpoint");
    return false;
  }

  if (ep_cnt > ENDPOINT_MAX) {
    notify(NL_ERROR, false, "Too many endpoints, maximum is %d", ENDPOINT_MAX);
    return false;
  }

  result = true;

  // Populate the list of all network interfaces.
  errno = 0;
  if (getifaddrs(&ifaces) == -1) {
    notify(NL_ERROR, true, "Unable to populate the list of "
           "network interfaces");
    return false;
  }

  for (i = 0; i < ep_cnt; i++) {
    // Parse all endpoint parts.
    new = malloc(sizeof(*new));
    if (new == NULL) {
      notify(NL_ERROR, true, "Unable to allocate memory for an endpoint");
      return false;
    }

    // Parse the endpoint.
    if (!parse_endpoint(new, argv[ep_idx + i], ifaces)) {
      free(new);
      result = false;
      break;
    }

    // Add the new endpoint to the head of the endpoint list.
    new->ep_next = *eps;
    *eps = new;
  }

  // Release resources held by the interface list.
  freeifaddrs(ifaces);

  return result;
}

/// Find the multiplier for the selected unit for conversion to nanoseconds.
///
/// @param[out] mult multiplier
/// @param[in]  unit unit abbreviation
static void
parse_unit(uint64_t* mult, const char* unit)
{
  if (strcmp(unit, "ns") == 0) *mult = 1LL;
  if (strcmp(unit, "us") == 0) *mult = 1000LL;
  if (strcmp(unit, "ms") == 0) *mult = 1000LL * 1000;
  if (strcmp(unit, "s")  == 0) *mult = 1000LL * 1000 * 1000;
  if (strcmp(unit, "m")  == 0) *mult = 1000LL * 1000 * 1000 * 60;
  if (strcmp(unit, "h")  == 0) *mult = 1000LL * 1000 * 1000 * 60 * 60;
  if (strcmp(unit, "d")  == 0) *mult = 1000LL * 1000 * 1000 * 60 * 60 * 24;
}

/// Parse a time duration into a number of nanoseconds.
/// @return status code
///
/// @param[out] dur duration
/// @param[in]  inp input string
bool
parse_duration(uint64_t* dur, const char* inp)
{
  uint64_t num;
  uint64_t mult;
  uint64_t x;
  int n;
  int adv;
  char unit[3];

  memset(unit, '\0', sizeof(unit));

  // Separate the scalar and the unit of the duration.
  n = sscanf(inp, "%" SCNu64 "%2s%n", &num, unit, &adv);
  if (n == 0) {
    notify(NL_ERROR, false, "Unable to parse the scalar value of "
           "the duration");
    return false;
  }

  if (n == 1) {
    notify(NL_ERROR, false, "No unit specified");
    return false;
  }

  // Verify that the full input string was parsed.
  if (adv != (int)strlen(inp)) {
    notify(NL_ERROR, false, "The duration string contains excess characters");
    return false;
  }

  // Parse the unit of the duration value.
  mult = 0;
  parse_unit(&mult, unit);
  if (mult == 0) {
    notify(NL_ERROR, false, "Unknown unit %2s of the duration", unit);
    return false;
  }

  // Check for overflow of the value in nanoseconds.
  x = num * mult;
  if (x / mult != num) {
    notify(NL_ERROR, false, "Duration would overflow, maximum is %" PRIu64 
           " nanoseconds", UINT64_MAX);
    return false;
  }

  *dur = x;
  return true;
}
