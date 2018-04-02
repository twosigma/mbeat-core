// Copyright (c) 2017-2018 Two Sigma Open Source, LLC.
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
#include <getopt.h>

#include "types.h"
#include "common.h"
#include "parse.h"


// Default values for optional arguments.
#define DEF_BUFFER_SIZE           0 // Zero denotes the system default.
#define DEF_COUNT                 5 // Number of published datagrams.
#define DEF_SLEEP        1000000000 // One second pause between payloads.
#define DEF_OFFSET                0 // Payloads start at zero.
#define DEF_TIME_TO_LIVE         32 // Time-To-Live for published datagrams.
#define DEF_ERROR                 0 // Process exit on publishing error.
#define DEF_LOOP                  0 // Looping policy on localhost.
#define DEF_NOTIFY_LEVEL          1 // Log errors and warnings by default.
#define DEF_NOTIFY_COLOR          1 // Colors in the notification output.

// Command-line options.
static uint64_t op_buf;  ///< Socket send buffer size in bytes.
static uint64_t op_cnt;  ///< Number of publishing rounds.
static uint64_t op_slp;  ///< Sleep duration between publishing rounds.
static uint64_t op_ttl;  ///< Time-To-Live for published datagrams.
static uint64_t op_off;  ///< Offset of published payload sequence numbers.
static uint64_t op_key;  ///< Key of the current process.
static uint64_t op_port; ///< UDP port for all endpoints.
static uint8_t  op_err;  ///< Process exit policy on publishing error.
static uint8_t  op_loop; ///< Datagram looping policy on local host.
static uint8_t  op_nlvl; ///< Notification verbosity level.
static uint8_t  op_ncol; ///< Notification coloring policy.

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
    "  -b, --buffer-size BSZ    Send buffer size in bytes.\n"
    "  -c, --count CNT          Publish exactly CNT datagrams. (def=%d)\n"
    "  -e, --exit-on-error      Stop the process on publishing error.\n"
    "  -h, --help               Print this help message.\n"
    "  -k, --key KEY            Key for the current run. (def=random)\n"
    "  -l, --loopback           Turn on datagram looping.\n"
    "  -n, --no-color           Turn off colors in logging messages.\n"
    "  -o, --offset OFF         Payloads start with selected sequence number offset. (def=%d)\n"
    "  -p, --port NUM           UDP port to use for all endpoints. (def=%d)\n"
    "  -s, --sleep-time DUR     Sleep duration between published datagram rounds. (def=1s)\n"
    "  -t, --time-to-live TTL   Set the Time-To-Live for all published datagrams. (def=%d)\n"
    "  -v, --verbose            Increase the verbosity of the logging output.\n",
    MBEAT_VERSION_MAJOR,
    MBEAT_VERSION_MINOR,
    MBEAT_VERSION_PATCH,
    DEF_COUNT,
    DEF_OFFSET,
    MBEAT_PORT,
    DEF_TIME_TO_LIVE);
}

/// Generate a random key.
/// @return random 64-bit unsigned integer (non-zero)
static uint64_t
generate_key(void)
{
  uint64_t key;

  // Seed the psuedo-random number generator. The generated key is not intended
  // to be cryptographically safe - it is just intended to prevent publishers
  // from the same host to share the same key. The only situation that this
  // could still happen is if system PIDs loop over and cause the following
  // equation: t1 + pid1 == t2 + pid2, which was ruled to be unlikely.
  srand48(time(NULL) + getpid());

  // Generate a random key and ensure it is not a zero. The zero value
  // is internally used to represent the state where no filtering of keys 
  // is performed by the subscriber process.
  do {
    key = (uint64_t)lrand48() | ((uint64_t)lrand48() << 32);
  } while (key == 0);

  return key;
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
  struct option lopts[] = {
    {"buffer-size",   required_argument, NULL, 'b'},
    {"count",         required_argument, NULL, 'c'},
    {"exit-on-error", no_argument,       NULL, 'e'},
    {"help",          no_argument,       NULL, 'h'},
    {"key",           required_argument, NULL, 'k'},
    {"loopback",      no_argument,       NULL, 'l'},
    {"no-color",      no_argument,       NULL, 'n'},
    {"offset",        required_argument, NULL, 'o'},
    {"port",          required_argument, NULL, 'p'},
    {"sleep-time",    required_argument, NULL, 's'},
    {"time-to-live",  required_argument, NULL, 't'},
    {"verbose",       no_argument,       NULL, 'v'},
    {NULL, 0, NULL, 0}
  };

  // Set optional arguments to sensible defaults.
  op_buf  = DEF_BUFFER_SIZE;
  op_cnt  = DEF_COUNT;
  op_slp  = DEF_SLEEP;
  op_ttl  = DEF_TIME_TO_LIVE;
  op_off  = DEF_OFFSET;
  op_err  = DEF_ERROR;
  op_loop = DEF_LOOP;
  op_port = MBEAT_PORT;
  op_nlvl = nlvl = DEF_NOTIFY_LEVEL;
  op_ncol = ncol = DEF_NOTIFY_COLOR;
  op_key  = generate_key();

  while ((opt = getopt_long(argc, argv, "b:c:ehk:lno:p:s:t:v", lopts, NULL)) != -1) {
    switch (opt) {

      // Send buffer size.
      case 'b':
        if (parse_scalar(&op_buf, optarg, parse_memory_unit) == 0)
          return false;
        break;

      // Number of published datagrams.
      case 'c':
        if (parse_uint64(&op_cnt, optarg, 1, UINT64_MAX) == 0)
          return false;
        break;

      // Process exit on publish error.
      case 'e':
        op_err = 1;
        break;

      // Usage information.
      case 'h':
        print_usage();
        return false;

      // Key of the current run.
      case 'k':
        if (parse_uint64(&op_key, optarg, 1, UINT64_MAX) == 0)
          return false;
        break;

      // Enable the datagram looping on localhost.
      case 'l':
        op_loop = 1;
        break;

      // Turn off the notification coloring.
      case 'n':
        op_ncol = 0;
        break;

      // Offset for published payloads.
      case 'o':
        if (parse_uint64(&op_off, optarg, 0, UINT64_MAX) == 0)
          return false;
        break;

      // UDP port for all endpoints.
      case 'p':
        if (parse_uint64(&op_port, optarg, 0, 65535) == 0)
          return false;
        break;

      // Sleep duration between publishing rounds.
      case 's':
        if (parse_scalar(&op_slp, optarg, parse_time_unit) == 0)
          return false;
        break;

      // Time-To-Live for published datagrams.
      case 't':
        if (parse_uint64(&op_ttl, optarg, 0, 255) == 0)
          return false;
        break;

      // Logging verbosity level.
      case 'v':
        if (op_nlvl < NL_TRACE)
          op_nlvl++;
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
  nlvl = op_nlvl;
  ncol = op_ncol;

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
    if (op_buf != 0) {
      notify(NL_TRACE, false,
             "Setting socket send buffer to %" PRIu64 " bytes", op_buf);
      buf_size = (int)op_buf;
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
                   &op_loop, sizeof(op_loop)) == -1) {
      notify(NL_ERROR, true,
             "Unable to turn %s the localhost datagram delivery",
             op_loop ? "on" : "off");
      return false;
    }

    // Adjust the Time-To-Live setting to reach farther networks.
    ttl_set = (uint8_t)op_ttl;
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
/// @param[out] pl   payload
/// @param[in]  ep   endpoint
/// @param[in]  snum sequence iterator counter
static void
fill_payload(payload* pl, const endpoint* ep, const uint32_t snum)
{
  struct timespec rtv;
  struct timespec mtv;

  memset(pl, 0, sizeof(*pl));

  pl->pl_magic = htonl(MBEAT_PAYLOAD_MAGIC);
  pl->pl_fver  = MBEAT_PAYLOAD_VERSION;
  pl->pl_ttl   = op_ttl;
  pl->pl_mport = htons(op_port);
  pl->pl_maddr = htonl(ep->ep_maddr.s_addr);
  pl->pl_key   = htonll(op_key);
  pl->pl_snum  = htonll(snum);
  pl->pl_slen  = htonll(op_cnt);
  memcpy(pl->pl_iname, ep->ep_iname, sizeof(pl->pl_iname));
  memcpy(pl->pl_hname, hname, sizeof(pl->pl_hname));

  // Get the system clock value.
  clock_gettime(CLOCK_REALTIME, &rtv);

  // Get the steady clock value.
  #ifdef __linux__
    clock_gettime(CLOCK_MONOTONIC_RAW, &mtv);
  #else
    clock_gettime(CLOCK_MONOTONIC, &mtv);
  #endif

  pl->pl_rsec = htonll((uint64_t)rtv.tv_nsec +
                       (1000000000ULL * (uint64_t)rtv.tv_sec));
  pl->pl_msec = htonll((uint64_t)mtv.tv_nsec +
                       (1000000000ULL * (uint64_t)mtv.tv_sec));
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

  notify(NL_DEBUG, false, "Process ID is %" PRIiMAX, (intmax_t)getpid());
  notify(NL_DEBUG, false, "Hostname is %s", hname);
  notify(NL_DEBUG, false, "UDP port is %" PRIu64, op_port);
  notify(NL_DEBUG, false, "Key is %" PRIu64, op_key);
  notify(NL_DEBUG, false, "Time-To-Live is %" PRIu64, op_ttl);

  notify(NL_INFO, false, "Starting to publish %" PRIu64 " datagram%s",
         op_cnt, (op_cnt > 1 ? "s" : ""));

  convert_nanos(&ts, op_slp);

  // Prepare the address structure.
  addr.sin_port   = htons((uint16_t)op_port);
  addr.sin_family = AF_INET;

  // Publish the requested number of datagrams.
  for (c = 0; c < op_cnt; c++) {
    notify(NL_DEBUG, false, "Round %" PRIu64 "/%" PRIu64 " of datagrams",
           c + 1 + op_off, op_cnt + op_off);

    for (e = eps; e != NULL; e = e->ep_next) {
      fill_payload(&pl, e, c + op_off);

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
        notify(op_err ? NL_ERROR : NL_WARN, true,
               "Unable to publish datagram from interface %s to "
               "multicast group %s", e->ep_iname, inet_ntoa(e->ep_maddr));

        if (op_err)
          return false;
      }
    }

    // Do not sleep after the last round of datagrams.
    if (op_slp > 0 && c != (op_cnt - 1)) {
      notify(NL_TRACE, false, "Sleeping for %" PRIu64 " nanoseconds", op_slp);
      nanosleep(&ts, NULL);
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
