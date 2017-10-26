// Copyright (c) 2017 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <err.h>

#include "types.h"
#include "common.h"
#include "parse.h"


// Default values for optional arguments.
#define DEF_BUFFER_SIZE           0 // Zero denotes the system default.
#define DEF_COUNT                 5 // Number of published datagrams.
#define DEF_INTERVAL     1000000000 // One second publishing interval.
#define DEF_TIME_TO_LIVE         32 // Time-To-Live for published datagrams.
#define DEF_ERROR                 0 // Process exit on publishing error.
#define DEF_LOOP                  0 // Looping policy on localhost.
#define DEF_NOTIFY_LEVEL          1 // Log errors and warnings by default.

// Command-line options.
static uint64_t opbuf;  ///< Socket send buffer size in bytes.
static uint64_t opcnt;  ///< Number of publishing rounds.
static uint64_t opival; ///< Wait time between publishing rounds.
static uint64_t opttl;  ///< Time-To-Live for published datagrams.
static uint64_t opsid;  ///< Internal session ID of the current process.
static uint64_t opport; ///< UDP port for all endpoints.
static uint8_t  operr;  ///< Process exit policy on publishing error.
static uint8_t  oploop; ///< Datagram looping policy on local host.
static uint8_t  opnlvl; ///< Notification verbosity level.

/// Print the utility usage information to the standard output.
static void
print_usage(void)
{
  fprintf(stderr,
    "Multicast heartbeat publisher - v%d.%d.%d\n"
    "Send datagrams to selected network endpoints.\n\n"

    "Usage:\n"
    "  mpub [OPTIONS] iface=maddr [iface=maddr ...]\n\n"

    "Options:\n"
    "  -b BSZ  Send buffer size in bytes.\n"
    "  -c CNT  Publish exactly CNT datagrams. (def=%d)\n"
    "  -e      Stop the process on publishing error.\n"
    "  -h      Print this help message.\n"
    "  -i DUR  Time interval between published datagrams. (def=1s)\n"
    "  -l      Turn on datagram looping.\n"
    "  -p NUM  UDP port to use for all endpoints. (def=%d)\n"
    "  -s SID  Session ID for the current run. (def=random)\n"
    "  -t TTL  Set the Time-To-Live for all published datagrams. (def=%d)\n",
    MBEAT_VERSION_MAJOR,
    MBEAT_VERSION_MINOR,
    MBEAT_VERSION_PATCH,
    DEF_COUNT,
    MBEAT_PORT,
    DEF_TIME_TO_LIVE);
}

/// Generate a random session ID.
/// @return random 64-bit unsigned integer (non-zero)
static uint64_t
generate_sid(void)
{
  uint64_t sid;

  // Seed the psuedo-random number generator. The generated session ID is not
  // intended to be cryptographically safe - it is just intended to prevent
  // publishers from the same host to share the same session ID. The only
  // situation that this could still happen is if system PIDs loop over and
  // cause the following equation: t1 + pid1 == t2 + pid2, which was ruled to
  // be unlikely.
  srand48(time(NULL) + getpid());

  // Generate a random session key and ensure it is not a zero. The zero value
  // is internally used to represent the state where no filtering of session
  // IDs is performed by the subscriber process.
  do {
    sid = (uint64_t)lrand48() | ((uint64_t)lrand48() << 32);
  } while (sid == 0);

  return sid;
}

/// Parse the command-line options.
/// @return status code
///
/// @param[out] ep_cnt endpoint count
/// @param[out] ep_idx endpoint start index
/// @param[in]  argc   argument count
/// @param[in]  argv   argument vector
static bool
parse_args(int* ep_cnt, int* ep_idx, int argc, char* argv[])
{
  int opt;

  // Set optional arguments to sensible defaults.
  opbuf  = DEF_BUFFER_SIZE;
  opcnt  = DEF_COUNT;
  opival = DEF_INTERVAL;
  opttl  = DEF_TIME_TO_LIVE;
  operr  = DEF_ERROR;
  oploop = DEF_LOOP;
  opport = MBEAT_PORT;
  opnlvl = DEF_NOTIFY_LEVEL;
  opsid  = generate_sid();

  while ((opt = getopt(argc, argv, "b:c:ehi:lp:s:t:v")) != -1) {
    switch (opt) {

      // Send buffer size.
      case 'b':
        if (parse_uint64(&opbuf, optarg, 0, UINT64_MAX) == 0)
          return false;
        break;

      // Number of published datagrams.
      case 'c':
        if (parse_uint64(&opcnt, optarg, 1, UINT64_MAX) == 0)
          return false;
        break;

      // Process exit on publish error.
      case 'e':
        operr = 1;
        break;

      // Usage information.
      case 'h':
        print_usage();
        return false;

      // Wait interval between datagrams in milliseconds.
      case 'i':
        if (parse_duration(&opival, optarg) == 0)
          return false;
        break;

      // Enable the datagram looping on localhost.
      case 'l':
        oploop = 1;
        break;

      // UDP port for all endpoints.
      case 'p':
        if (parse_uint64(&opport, optarg, 0, 65535) == 0)
          return false;
        break;

      // Session ID of the current run.
      case 's':
        if (parse_uint64(&opsid, optarg, 1, UINT64_MAX) == 0)
          return false;
        break;

      // Time-To-Live for published datagrams.
      case 't':
        if (parse_uint64(&opttl, optarg, 0, 255) == 0)
          return false;
        break;

      // Logging verbosity level.
      case 'v':
        if (opnlvl < NL_TRACE)
          opnlvl++;
        break;

      // Unknown option.
      case '?':
        fprintf(stderr, "Invalid option '%c'.\n", optopt);
        print_usage();
        return false;

      // Unknown situation.
      default:
        print_usage();
        return false;
    }
  }

  // Set the requested global logging level threshold.
  glvl = opnlvl;

  *ep_cnt = argc - optind;
  *ep_idx = optind;

  return true;
}

/// Create endpoint sockets and apply the interface settings.
/// @return status code
///
/// @param[in] eps  endpoint list
static bool
create_sockets(endpoint* eps)
{
  int enable;
  uint8_t ttl_set;
  int buf_size;
  endpoint* ep;

  enable = 1;
  for (ep = eps; ep != NULL; ep = ep->ep_next) {
    notify(NL_INFO, false,
           "Creating endpoint on interface %s for multicast group %s",
           ep->ep_iname, inet_ntoa(ep->ep_maddr));

    // Create a UDP socket.
    ep->ep_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ep->ep_sock == -1) {
      notify(NL_ERROR, true, "Unable to create socket");
      return false;
    }

    // Enable multiple sockets being bound to the same address/port.
    if (setsockopt(ep->ep_sock, SOL_SOCKET, SO_REUSEADDR,
                   &enable, sizeof(enable)) == -1) {
      notify(NL_ERROR, true, "Unable to set the socket address reusable");
      return false;
    }

    // Set the socket send buffer size to the requested value.
    if (opbuf != 0) {
      buf_size = (int)opbuf;
      if (setsockopt(ep->ep_sock, SOL_SOCKET, SO_SNDBUF,
                     &buf_size, sizeof(buf_size)) == -1) {
        notify(NL_ERROR, true,
               "Unable to set the socket send buffer size to %d", buf_size);
        return false;
      }
    }

    // Limit the socket to the selected interface.
    if (setsockopt(ep->ep_sock, IPPROTO_IP, IP_MULTICAST_IF,
                   &(ep->ep_iaddr), sizeof(ep->ep_iaddr)) == -1) {
      notify(NL_ERROR, true, "Unable to set the socket interface to %s",
             ep->ep_iname);
      return false;
    }

    // Set the datagram looping policy.
    if (setsockopt(ep->ep_sock, IPPROTO_IP, IP_MULTICAST_LOOP,
                   &oploop, sizeof(oploop)) == -1) {
      notify(NL_ERROR, true,
             "Unable to turn %s the localhost datagram delivery",
             oploop ? "on" : "off");
      return false;
    }

    // Adjust the Time-To-Live setting to reach farther networks.
    ttl_set = (uint8_t)opttl;
    if (setsockopt(ep->ep_sock, IPPROTO_IP, IP_MULTICAST_TTL,
                   &ttl_set, sizeof(ttl_set)) == -1) {
      notify(NL_ERROR, true,
             "Unable to set Time-To-Live of datagrams to %" PRIu8, ttl_set);
      return false;
    }
  }

  return true;
}

/// Create the datagram payload.
///
/// @param[out] pl    payload
/// @param[in]  ep    endpoint
/// @param[in]  sid   session ID
static void
fill_payload(payload* pl, const endpoint* ep, const uint32_t snum)
{
  struct timespec tv;

  memset(pl, 0, sizeof(*pl));

  pl->pl_magic = htonl(MBEAT_PAYLOAD_MAGIC);
  pl->pl_fver  = MBEAT_PAYLOAD_VERSION;
  pl->pl_ttl   = opttl;
  pl->pl_mport = htons(opport);
  pl->pl_maddr = htonl(ep->ep_maddr.s_addr);
  pl->pl_sid   = htonll(opsid);
  pl->pl_snum  = htonll(snum);
  pl->pl_slen  = htonll(opcnt);
  memcpy(pl->pl_iname, ep->ep_iname, sizeof(pl->pl_iname));
  memcpy(pl->pl_hname, hname, sizeof(pl->pl_hname));

  clock_gettime(CLOCK_REALTIME, &tv);
  pl->pl_sec   = htonll((uint64_t)tv.tv_sec);
  pl->pl_nsec  = htonl((uint32_t)tv.tv_nsec);
}

/// Publish datagrams to all requested multicast groups.
/// @return status code
///
/// @param[in] eps endpoint list
static bool
publish_datagrams(endpoint* eps)
{
  uint64_t c;
  ssize_t ret;
  payload pl;
  struct timespec ts;
  struct sockaddr_in addr;
  struct msghdr msg;
  struct iovec data;
  endpoint* e;

  notify(NL_DEBUG, false, "Hostname is %s", hname);
  notify(NL_DEBUG, false, "UDP port is %" PRIu64, opport);
  notify(NL_DEBUG, false, "Session ID is %" PRIu64, opsid);
  notify(NL_DEBUG, false, "Time-To-Live is %" PRIu64, opttl);

  notify(NL_INFO, false, "Starting to publish %" PRIu64 " datagram%s",
         opcnt, (opcnt > 1 ? "s" : ""));

  convert_nanos(&ts, opival);

  // Prepare the address structure.
  addr.sin_port   = htons((uint16_t)opport);
  addr.sin_family = AF_INET;

  // Publish the requested number of datagrams.
  for (c = 0; c < opcnt; c++) {
    notify(NL_DEBUG, false, "Round %" PRIu64 "/%" PRIu64 " of datagrams",
           c + 1, opcnt);

    for (e = eps; e != NULL; e = e->ep_next) {
      fill_payload(&pl, e, c);

      // Set the multicast address.
      addr.sin_addr.s_addr = e->ep_maddr.s_addr;

      // Prepare payload data.
      data.iov_base = &pl;
      data.iov_len  = sizeof(pl);

      // Prepare the message.
      msg.msg_name       = &addr;
      msg.msg_namelen    = sizeof(addr);
      msg.msg_iov        = &data;
      msg.msg_iovlen     = 1;
      msg.msg_control    = NULL;
      msg.msg_controllen = 0;

      // Publish the payload.
      notify(NL_TRACE, false,
             "Publishing datagram from interface %s to multicast group %s",
             e->ep_iname, inet_ntoa(e->ep_maddr));

      ret = sendmsg(e->ep_sock, &msg, MSG_DONTWAIT);
      if (ret == 0) {
        notify(operr ? NL_ERROR : NL_WARN, true,
               "Unable to publish datagram from interface %s to "
               "multicast group %s", e->ep_iname, inet_ntoa(e->ep_maddr));

        if (operr)
          return false;
      }
    }

    // Do not sleep after the last round of datagrams.
    if (opival > 0 && c != (opcnt - 1)) {
      nanosleep(&ts, NULL);
      notify(NL_TRACE, false, "Sleeping for %" PRIu64 " nanoseconds", opival);
    }
  }

  notify(NL_INFO, false, "Finished publishing of all datagrams");
  return true;
}

/// Multicast heartbeat publisher.
int
main(int argc, char* argv[])
{
  // Endpoint list.
  endpoint* eps;

  int ep_cnt;
  int ep_idx;

  eps = NULL;
  ep_cnt = 0;
  ep_idx = 0;

  // Process the command-line arguments.
  if (!parse_args(&ep_cnt, &ep_idx, argc, argv))
    return EXIT_FAILURE;

  // Obtain the hostname.
  if (!cache_hostname())
    return EXIT_FAILURE;

  // Parse and validate endpoints.
  if (!parse_endpoints(&eps, ep_idx, argv, ep_cnt))
    return EXIT_FAILURE;

  // Initialise the sockets based on selected interfaces.
  if (!create_sockets(eps))
    return EXIT_FAILURE;

  // Publish datagrams to selected multicast groups.
  if (!publish_datagrams(eps))
    return EXIT_FAILURE;

  free_endpoints(eps);

  return EXIT_SUCCESS;
}
