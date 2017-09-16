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
    warn("Unable to parse a number from string '%s'", str);
    return false;
  }

  // Verify that the number belongs to the specified range.
  if (x < (uintmax_t)min || x > (uintmax_t)max) {
    warnx("Number %ju out of range (%" PRIu64 "..%" PRIu64 ")", x, min, max);
    return false;
  }

  *out = (uint64_t)x;
  return true;
}

/// Find the lesser of two unsigned integers.
/// @return lesser of the two numbers
/// 
/// @param[in] a first number
/// @param[in] b second number
static size_t
lesser(const size_t a, const size_t b)
{
  return a < b ? a : b;
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

    if (strcmp(inp, iface->ifa_name) == 0)
      break;
  }

  // If no suitable interface was found.
  if (iface == NULL) {
    warnx("Unable to find interface '%s' with an IPv4 address", inp);
    return false;
  }

  // Make sure that the interface is up.
  if (!(iface->ifa_flags & IFF_UP)) {
    warnx("Interface '%s' is not up", inp);
    return false;
  }

  // Make sure that the interface supports multicast traffic.
  if (!(iface->ifa_flags & IFF_MULTICAST)) {
    warnx("Interface '%s' is not available for multicast traffic", inp);
    return false;
  }

  // Assign the found interface address to the endpoint array.
  if_addr_in = (struct sockaddr_in*)iface->ifa_addr;
  ep->ep_iaddr = if_addr_in->sin_addr;

  // Copy the interface name to the endpoint.
  memcpy(ep->ep_iname, inp, lesser(INAME_LEN, strlen(inp)));
  
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
    warnx("Unable to parse the multicast address '%s'", inp);
    return false;
  }

  // Ensure that the address belongs to the multicast range.
  if (!IN_MULTICAST(ntohl(ep->ep_maddr.s_addr))) {
    warnx("Address '%s' does not belong to the multicast range", inp);
    return false;
  }

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
parse_endpoints(endpoint* eps, const int ep_idx, char* argv[], const int ep_cnt)
{
  int i;
  int parts;
  bool result;
  char iname[256];
  char maddr[256];
  struct ifaddrs* ifaces;

  result = true;

  // Populate the list of all network interfaces.
  errno = 0;
  if (getifaddrs(&ifaces) == -1) {
    warn("Unable to populate the list of network interfaces");
    return false;
  }

  for (i = 0; i < ep_cnt; i++) {
    // Reset all buffers.
    memset(iname, '\0', 256);
    memset(maddr, '\0', 256);

    // Parse the endpoint format.
    parts = sscanf(argv[ep_idx + i], "%255[^=]=%255[^:]", iname, maddr);
    if (parts != 2) {
      warnx("Unable to parse the endpoint '%s'", argv[ep_idx + i]);
      result = false;
      break;
    }

    // Parse all endpoint parts.
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
  n = sscanf(inp, "%llu%2s%n", &num, unit, &adv);
  if (n == 0) {
    warnx("Unable to parse the scalar value of the duration");
    return false;
  }

  if (n == 1) {
    warnx("Unable to parse the unit of the duration");
    return false;
  }

  // Verify that the full input string was parsed.
  if (adv != (int)strlen(inp)) {
    warnx("The duration string contains too many characters");
    return false;
  }

  // Parse the unit of the duration value.
  mult = 0;
  parse_unit(&mult, unit);
  if (mult == 0) {
    warnx("Unknown unit of the duration");
    return false;
  }

  // Check for overflow of the value in nanoseconds.
  x = num * mult;
  if (x / mult != num) {
    warnx("Duration is too long");
    return false;
  }

  *dur = x;
  return true; 
}