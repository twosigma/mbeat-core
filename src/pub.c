/*
 *  Copyright (c) 2017 Two Sigma Open Source, LLC.
 *  All Rights Reserved
 *
 *  Distributed under the terms of the 2-clause BSD License. The full
 *  license is in the file LICENSE, distributed as part of this software.
**/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

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
#include <time.h>
#include <err.h>
#include <endian.h>

#include "types.h"
#include "common.h"


/* Default values for optional arguments. */
#define DEF_BUFFER_SIZE     0 /* Zero denotes the system default.      */
#define DEF_COUNT           5 /* Number of published datagrams.        */
#define DEF_INTERVAL     1000 /* Publishing interval in milliseconds.  */
#define DEF_TIME_TO_LIVE    1 /* Time-To-Live for published datagrams. */
#define DEF_LOOP            0 /* Looping policy on localhost.          */

/** Print the utility usage information to the standard output. */
static void
print_usage(void)
{
  fprintf(stderr, "multicast heartbeat publisher - send datagrams to selected"
                  " network endpoints - v%d.%d.%d\n\n", MBEAT_VERSION_MAJOR,
                  MBEAT_VERSION_MINOR, MBEAT_VERSION_PATCH);
  fprintf(stderr, "mbeat_pub [-b BSZ] [-c CNT] [-h] [-i MS] [-l] [-p PORT]"
                  " [-s SID] [-t TTL] iface=maddr [iface=maddr ...]\n");
  fprintf(stderr, "  -b BSZ  Send buffer size in bytes.\n");
  fprintf(stderr, "  -c CNT  Publish exactly CNT datagrams."
                  " (def=%d)\n", DEF_COUNT);
  fprintf(stderr, "  -h      Print this help message.\n");
  fprintf(stderr, "  -i MS   Interval between published datagrams in"
                  " milliseconds. (def=%d)\n", DEF_INTERVAL);
  fprintf(stderr, "  -l      Turn on datagram looping.\n");
  fprintf(stderr, "  -p PORT UDP port to use for all endpoints."
		  " (def=%d)\n", MBEAT_PORT);
  fprintf(stderr, "  -s SID  Session ID for the current run. (def=random)\n");
  fprintf(stderr, "  -t TTL  Set the Time-To-Live for all published datagrams."
                  " (def=%d)\n", DEF_TIME_TO_LIVE);
}

/** Generate a random session ID.
 *
 * @return random 32-bit integer (non-zero)
**/
static uint32_t
generate_sid(void)
{
  uint32_t sid;

  /* Seed the psuedo-random number generator. The generated session ID is not
   * intended to be cryptographically safe - it is just intended to prevent
   * publishers from the same host to share the same session ID. The only
   * situation that this could still happen is if system PIDs loop over and
   * cause the following equation: t1 + pid1 == t2 + pid2, which was ruled to
   * be unlikely. */
  srand48(time(NULL) + getpid());

  /* Generate a random session key and ensure it is not a zero. The zero value
   * is internally used to represent the state where no filtering of session
   * IDs is performed by the subscriber process. */
  do {
    sid = (uint32_t)lrand48();
  } while (sid == 0);

  return sid;
}

/** Parse the command-line options.
 *
 * @param[out] ep_cnt endpoint count
 * @param[out] ep_idx endpoint start index 
 * @param[out] opts   command-line options
 * @param[in]  argc   argument count
 * @param[in]  argv   argument vector
 *
 * @return status code
**/
static bool 
parse_args(int* ep_cnt, int* ep_idx, pub_options* opts, int argc, char* argv[])
{
  int opt;

  /* Set optional arguments to sensible defaults. */
  opts->po_buf = DEF_BUFFER_SIZE;
  opts->po_cnt = DEF_COUNT;
  opts->po_int = DEF_INTERVAL;
  opts->po_ttl = DEF_TIME_TO_LIVE;
  opts->po_lop = DEF_LOOP;
  opts->po_port = MBEAT_PORT;
  opts->po_sid = generate_sid();

  while ((opt = getopt(argc, argv, "b:c:hi:lp:s:t:")) != -1) {
    switch (opt) {

      /* Send buffer size. The lowest accepted value is 1024, enforcing the
       * same limit as the Linux kernel. */
      case 'b':
        if (parse_uint32(&opts->po_buf, optarg, 1024, UINT32_MAX) == 0)
          return false;
        break;

      /* Number of published datagrams. */
      case 'c':
        if (parse_uint32(&opts->po_cnt, optarg, 1, UINT32_MAX) == 0)
          return false;
        break;

      /* Usage information. */
      case 'h':
        print_usage();
        return false;

      /* Wait interval between datagrams in milliseconds. */
      case 'i':
        if (parse_uint32(&opts->po_int, optarg, 0, UINT32_MAX) == 0)
          return false;
        break;

      /* Enable the datagram looping on localhost. */
      case 'l':
        opts->po_lop = 1;
        break;

      /* UDP port for all endpoints. */
      case 'p':
        if (parse_uint32(&opts->po_port, optarg, 0, 65535) == 0)
          return false;
        break;

      /* Session ID of the current run. */
      case 's':
        if (parse_uint32(&opts->po_sid, optarg, 1, UINT32_MAX) == 0)
          return false;
        break;

      /* Time-To-Live for published datagrams. */
      case 't':
        if (parse_uint32(&opts->po_ttl, optarg, 0, 255) == 0)
          return false;
        break;

      /* Unknown option. */
      case '?':
        warnx("Invalid option '%c'", optopt);
        print_usage();
        return false;

      /* Unknown situation. */
      default:
        print_usage();
        return false;
    }
  }

  *ep_cnt = argc - optind;
  *ep_idx = optind;

  return true;
}

/** Create endpoint sockets and apply the interface settings.
 *
 * @param[in] eps    endpoint array
 * @param[in] ep_cnt number of endpoint elements
 * @param[in] opts   command-line options
 *
 * @return status code
**/
static bool 
create_sockets(endpoint* eps, const int ep_cnt, const pub_options* opts)
{
  int i;
  int enable;
  uint8_t ttl_set;
  int buf_size;

  enable = 1;
  for (i = 0; i < ep_cnt; i++) {
    eps[i].ep_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (eps[i].ep_sock == -1) {
      warn("Unable to create socket");
      return false;
    }

    /* Enable multiple sockets being bound to the same address/port. */
    if (setsockopt(eps[i].ep_sock, SOL_SOCKET, SO_REUSEADDR,
                   &enable, sizeof(enable)) == -1) {
      warn("Unable to make the socket address reusable");
      return false;
    }

    /* Set the socket send buffer size to the requested value. */
    if (opts->po_buf != 0) {
      buf_size = (int)opts->po_buf;
      if (setsockopt(eps[i].ep_sock, SOL_SOCKET, SO_SNDBUF,
                     &buf_size, sizeof(buf_size)) == -1) {
        warn("Unable to set the socket send buffer size to %d", buf_size);
        return false;
      }
    }

    /* Limit the socket to the selected interface. */
    if (setsockopt(eps[i].ep_sock, IPPROTO_IP, IP_MULTICAST_IF,
                   &(eps[i].ep_iaddr), sizeof(eps[i].ep_iaddr)) == -1) {
      warn("Unable to select socket interface '%s'", eps[i].ep_iname);
      return false;
    }

    /* Set the datagram looping policy. */
    if (setsockopt(eps[i].ep_sock, IPPROTO_IP, IP_MULTICAST_LOOP,
                   &opts->po_lop, sizeof(opts->po_lop)) == -1) {
      warn("Unable to set the localhost looping policy");
      return false;
    }

    /* Adjust the Time-To-Live setting to reach farther networks. */
    ttl_set = (uint8_t)opts->po_ttl;
    if (setsockopt(eps[i].ep_sock, IPPROTO_IP, IP_MULTICAST_TTL,
                   &ttl_set, sizeof(ttl_set)) == -1) {
      warn("Unable to set the Time-To-Live of datagrams");
      return false;
    }
  }

  return true;
}

/** Create the datagram payload.
 *
 * @param[out] pl    payload
 * @param[in]  ep    endpoint
 * @param[in]  sid   session ID
 * @param[in]  hname hostname
 * @param[in]  opts  command-line options
**/
static void
fill_payload(payload* pl,
             endpoint* ep,
             const uint32_t snum,
             const char* hname,
             const pub_options* opts)
{
  struct timespec tv;

  memset(pl, 0, sizeof(*pl));

  pl->pl_fver  = htobe16(PAYLOAD_VERSION);
  pl->pl_snum  = htobe32(snum);
  pl->pl_sid   = htobe32(opts->po_sid);
  pl->pl_maddr = htobe32(ep->ep_maddr.s_addr);
  pl->pl_mport = htobe16(opts->po_port);
  memcpy(pl->pl_iname, ep->ep_iname, sizeof(pl->pl_iname));
  memcpy(pl->pl_hname, hname, sizeof(pl->pl_hname));

  clock_gettime(CLOCK_REALTIME, &tv);
  pl->pl_sec   = htobe64((uint64_t)tv.tv_sec);
  pl->pl_nsec  = htobe32((uint32_t)tv.tv_nsec);
}

/** Publish datagrams to all requested multicast groups.
 *
 * @param[in] eps    endpoint array
 * @param[in] ep_cnt number of endpoint entries
 * @param[in] hname  local hostname
 * @param[in] opts   command-line options
 *
 * @return status code
**/
static bool
publish_datagrams(endpoint* eps,
                  const int ep_cnt,
                  const char* hname,
                  const pub_options* opts)
{
  uint32_t c;
  int i;
  payload pl;
  struct timespec ts;
  struct sockaddr_in addr;

  convert_millis(&ts, opts->po_int);

  /* Prepare the address structure. */
  addr.sin_port   = htons((uint16_t)opts->po_port);
  addr.sin_family = AF_INET;

  /* Publish the requested number of datagrams. */
  for (c = 0; c < opts->po_cnt; c++) {
    for (i = 0; i < ep_cnt; i++) {
      fill_payload(&pl, &eps[i], c, hname, opts);

      addr.sin_addr.s_addr = eps[i].ep_maddr.s_addr;
      if (sendto(eps[i].ep_sock,                        // socket
                 &pl, sizeof(pl),                       // payload
                 MSG_DONTWAIT,                          // flags
                 (struct sockaddr*)&addr, sizeof(addr)) // address
                 == -1) {
        warn("Unable to send datagram");
        return false;
      }
    }

    /* Do not sleep after the last round of datagrams. */
    if (opts->po_int > 0 && c != (opts->po_cnt - 1))
      nanosleep(&ts, NULL);
  }

  return true;
}

/** Multicast heartbeat publisher. */
int
main(int argc, char* argv[])
{
  /* Command-line options. */
  pub_options opts;

  /* Endpoint array. */
  endpoint* eps;
  int ep_cnt;
  int ep_idx;

  /* Cached hostname. */
  char hname[HNAME_LEN + 1];

  eps = NULL;
  ep_cnt = 0;
  ep_idx = 0;

  /* Obtain the hostname. */
  if (!cache_hostname(hname, sizeof(hname)))
    return EXIT_FAILURE;

  /* Process the command-line arguments. */
  if (!parse_args(&ep_cnt, &ep_idx, &opts, argc, argv))
    return EXIT_FAILURE;

  /* Allocate memory for endpoints. */
  if (!allocate_endpoints(&eps, ep_cnt))
    return EXIT_FAILURE;

  /* Parse and validate endpoints. */
  if (!parse_endpoints(eps, ep_idx, argv, ep_cnt))
    return EXIT_FAILURE;

  /* Initialise the sockets based on selected interfaces. */
  if (!create_sockets(eps, ep_cnt, &opts))
    return EXIT_FAILURE;

  /* Publish datagrams to selected multicast groups. */
  if (!publish_datagrams(eps, ep_cnt, hname, &opts))
    return EXIT_FAILURE;

  free(eps);

  return EXIT_SUCCESS;
}
