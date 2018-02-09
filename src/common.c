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
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <inttypes.h>

#include "common.h"
#include "types.h"


// Notification settings.
uint8_t nlvl; ///< Minimal level threshold.
uint8_t ncol; ///< Colouring policy.

/// Hostname.
char hname[HNAME_LEN];

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
bool
cache_hostname(void)
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

/// Add ASCII escape sequences to highlight every substitution in a
/// printf-formatted string.
///
/// @param[out] out highlighted string
/// @param[in]  in  string
static void
highlight(char* out, const char* inp)
{
  char* pcent;
  char* delim;
  char* str;
  char cpy[128];
  char hold;

  // As the input string is likely to be a compiler literal, and therefore
  // is stored in a read-only memory, we have to make a copy of it.
  memset(cpy, '\0', sizeof(cpy));
  strcpy(cpy, inp);

  str = cpy;
  while (str != NULL) {
    // Find the next substitution.
    pcent = strchr(str, '%');
    if (pcent != NULL) {
      // Copy the text leading to the start of the substitution.
      *pcent = '\0';
      strcat(out, str);

      // Append the ASCII escape code to start the highlighting.
      strcat(out, "\x1b[1m");

      // Locate the substitution's end and copy the contents.
      *pcent = '%';
      delim = strpbrk(pcent, "cdfghopsu");
      if (delim != NULL) {
        delim++;
        hold = *delim;
        *delim = '\0';
      }
      strcat(out, pcent);

      // Insert the ASCII escape code to end the highlighting.
      strcat(out, "\x1b[0m");

      // Move back the space and continue.
      if (delim != NULL) {
        *delim = hold;
        str = delim;
      } else {
        str = NULL;
      }
    } else {
      // Since there are no more substitutions in the input string, copy the
      // rest of it and finish.
      strcat(out, str);
      break;
    }
  }
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
  char hfmt[128];
  char msg[128];
  char errmsg[128];
  struct tm* tfmt;
  struct timespec tspec;
  va_list args;
  int save;
  static const char* lname[] = {"ERROR", " WARN", " INFO", "DEBUG", "TRACE"};
  static const int lcol[]    = {31, 33, 32, 34, 35};
  char lstr[32];

  // Ignore messages that fall below the global threshold.
  if (lvl > nlvl)
    return;

  // Save the errno with which the function was called.
  save = errno;

  // Obtain and format the current time in GMT.
  clock_gettime(CLOCK_REALTIME, &tspec);
  tfmt = gmtime(&tspec.tv_sec);
  strftime(tstr, sizeof(tstr), "%T", tfmt);

  // Prepare highlights for the message variables.
  memset(hfmt, '\0', sizeof(hfmt));
  if (ncol)
    highlight(hfmt, fmt);
  else
    memcpy(hfmt, fmt, strlen(fmt));

  // Fill in the passed message.
  va_start(args, fmt);
  vsprintf(msg, hfmt, args);
  va_end(args);

  // Obtain the errno message.
  memset(errmsg, '\0', sizeof(errmsg));
  if (perr)
    sprintf(errmsg, ": %s", strerror(save));

  // Format the level name.
  memset(lstr, '\0', sizeof(lstr));
  if (ncol)
    sprintf(lstr, "\x1b[%dm%s\x1b[0m", lcol[lvl], lname[lvl]);
  else
    memcpy(lstr, lname[lvl], strlen(lname[lvl]));

  // Print the final log line.
  (void)fprintf(stderr, "[%s.%03" PRIu32 "] %s - %s%s\n",
                tstr, (uint32_t)tspec.tv_nsec / 1000000, lstr, msg, errmsg);
}
