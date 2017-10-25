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
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include "common.h"


/// Global notification level threshold.
uint8_t glvl;

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
/// @param[out] hname hostname
bool
cache_hostname(char* hname)
{
  memset(hname, '\0', HNAME_LEN);

  if (gethostname(hname, HNAME_LEN) == -1) {
    if (errno == ENAMETOOLONG) {
      notify(NL_WARN, false, "Truncated hostname to %d letters", HNAME_LEN);
    } else {
      notify(NL_ERROR, true, "Unable to get the local hostname");
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

/// Issue a notification to the standard error stream.
///
/// @param[in] lvl  notification level (one of NL_*)
/// @param[in] perr append the errno string to end of the notification 
/// @param[in] msg  message to print
/// @param[in] ...  arguments for the message
void
notify(const uint8_t lvl, const bool perr, const char* fmt, ...)
{
  char tstr[32];
  char msg[128];
  char errmsg[128];
  struct tm* tfmt;
  time_t traw;
  va_list args;
  int save;
  static const char* lname[] = {"ERROR", " WARN", " INFO", "DEBUG", "TRACE"};

  // Ignore messages that fall below the global threshold.
  if (lvl > glvl)
    return;

  // Save the errno with which the function was called.
  save = errno;

  // Obtain and format the current time in GMT.
  traw = time(NULL);
  tfmt = gmtime(&traw);
  strftime(tstr, sizeof(tstr), "%F %T", tfmt);

  // Fill in the passed message.
  va_start(args, fmt);
  vsprintf(msg, fmt, args);
  va_end(args);

  // Obtain the errno message.
  memset(errmsg, '\0', sizeof(errmsg));
  if (perr)
    sprintf(errmsg, ": %s", strerror(save));

  // Print the final log line.
  (void)fprintf(stderr, "[%s] %s - %s%s\n", tstr, lname[lvl], msg, errmsg);
}
