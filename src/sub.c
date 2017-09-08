/*
 *  Copyright (c) 2017 Two Sigma Open Source, LLC.
 *  All Rights Reserved
 *
 *  Distributed under the terms of the 2-clause BSD License. The full
 *  license is in the file LICENSE, distributed as part of this software.
**/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/signalfd.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <time.h>
#include <string.h>
#include <err.h>

#include "types.h"
#include "common.h"


/* Default values for optional arguments. */
#define DEF_TIMEOUT      0 /* Zero denotes no timeout is applied.           */
#define DEF_BUFFER_SIZE  0 /* Zero denotes the system default.              */
#define DEF_SESSION_ID   0 /* Zero denotes no session ID filtering.         */
#define DEF_EXPECT_COUNT 0 /* Zero denotes no particular count is expected. */
#define DEF_OFFSET       0 /* Sequence numbers have no offset by default.   */
#define DEF_RAW_OUTPUT   0 /* Raw binary output is disabled by default.     */
#define DEF_UNBUFFERED   0 /* Unbuffered output is disabled by default.     */

/** Print the utility usage information to the standard output. */
static void
print_usage(void)
{
  fprintf(stderr, "multicast heartbeat subscriber - receive datagrams from"
                  " selected network endpoints - v%d.%d.%d\n\n",
                  MBEAT_VERSION_MAJOR, MBEAT_VERSION_MINOR,
                  MBEAT_VERSION_PATCH);
  fprintf(stderr, "mbeat_sub [-b BSZ] [-e CNT] [-h] [-o OFF] [-p PORT] [-r]"
                  " [-s SID] [-t MS] [-u] iface=maddr [iface=maddr ...]\n");
  fprintf(stderr, "  -b BSZ  Receive buffer size in bytes.\n");
  fprintf(stderr, "  -e CNT  Quit after CNT datagrams were received.\n");
  fprintf(stderr, "  -h      Print this help message.\n");
  fprintf(stderr, "  -o OFF  Ignore payloads with lesser sequence number.\n");
  fprintf(stderr, "  -p PORT UDP port for all endpoints. (def=%d)\n",
                  MBEAT_PORT);
  fprintf(stderr, "  -r      Output the data in raw binary format.\n");
  fprintf(stderr, "  -s SID  Only report datagrams with this session ID.\n");
  fprintf(stderr, "  -t MS   Timeout of the process after MS milliseconds.\n");
  fprintf(stderr, "  -u      Disable output buffering.\n");
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
parse_args(int* ep_cnt, int* ep_idx, sub_options* opts, int argc, char* argv[])
{
  int opt;

  /* Set optional arguments to sensible defaults. */
  opts->so_tout = DEF_TIMEOUT;
  opts->so_buf  = DEF_BUFFER_SIZE;
  opts->so_sid  = DEF_SESSION_ID;
  opts->so_exp  = DEF_EXPECT_COUNT;
  opts->so_off  = DEF_OFFSET;
  opts->so_port = MBEAT_PORT;
  opts->so_raw  = DEF_RAW_OUTPUT;
  opts->so_unb  = DEF_UNBUFFERED;

  while ((opt = getopt(argc, argv, "b:e:ho:p:rs:t:u")) != -1) {
    switch (opt) {

      /* Receive buffer size. The lowest accepted value is 128, enforcing the
       * same limit as the Linux kernel. */
      case 'b':
        if (parse_uint32(&opts->so_buf, optarg, 128, UINT32_MAX) == 0)
          return false;
        break;

      /* Expected number of datagrams to receive. */
      case 'e':
        if (parse_uint32(&opts->so_exp, optarg, 1, UINT32_MAX) == 0)
          return false;
        break;

      /* Usage information. */
      case 'h':
        print_usage();
        return false;

      /* Sequence number offset. */
      case 'o':
        if (parse_uint32(&opts->so_off, optarg, 1, UINT32_MAX) == 0)
          return false;
        break;

      /* UDP port for all endpoints. */
      case 'p':
        if (parse_uint32(&opts->so_port, optarg, 0, 65535) == 0)
          return false;
        break;

      /* Raw binary output option. */
      case 'r':
        opts->so_raw = 1;
        break;

      /* Session ID of the current run. */
      case 's':
        if (parse_uint32(&opts->so_sid, optarg, 1, UINT32_MAX) == 0)
          return false;
        break;

      /* Timeout for the process. */
      case 't':
        if (parse_uint32(&opts->so_tout, optarg, 1, UINT32_MAX) == 0)
          return false;
        break;

      /* Unbuffered output option. */
      case 'u':
        opts->so_unb = 1;
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
 * @param[in] ep_cnt number of endpoint entries
 * @param[in] opts   command-line options
 *
 * @return status code
**/
static bool 
create_sockets(endpoint* eps, const int ep_cnt, const sub_options* opts)
{
  int i;
  int enable;
  int buf_size;
  struct sockaddr_in addr;
  struct ip_mreq req;
  char* mcast_str;

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

    /* Set the socket receive buffer size to the requested value. */
    if (opts->so_buf != 0) {
      buf_size = (int)opts->so_buf;
      if (setsockopt(eps[i].ep_sock, SOL_SOCKET, SO_RCVBUF,
                     &buf_size, sizeof(buf_size)) == -1) {
        warn("Unable to set the socket receive buffer size to %d", buf_size);
        return false;
      }
    }

    mcast_str = inet_ntoa(eps[i].ep_maddr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)opts->so_port);
    addr.sin_addr   = eps[i].ep_maddr;

    /* Bind the socket to the multicast group. */
    if (bind(eps[i].ep_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
      warn("Unable to bind to address %s", mcast_str);
      return false;
    }

    /* Subscribe the socket to the multicast group. */
    req.imr_interface.s_addr = eps[i].ep_iaddr.s_addr;
    req.imr_multiaddr.s_addr = eps[i].ep_maddr.s_addr;
    if (setsockopt(eps[i].ep_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &req, sizeof(req)) == -1) {
      warn("Unable to join multicast group %s", mcast_str);
      return false;
    }
  }

  return true;
}

/** Create the event queue.
 *
 * @param[out] epfd event queue
 *
 * @return status code
**/
static bool
create_event_queue(int* epfd)
{
  *epfd = epoll_create(ENDPOINT_MAX);
  if (*epfd < 0) {
    warn("Unable to create event queue");
    return false;
  }

  return true;
}

/** Add the socket associated with each endpoint to the event queue.
 *
 * @param[in] epfd   event queue
 * @param[in] eps    endpoint array
 * @param[in] ep_cnt number of endpoint entries
 *
 * @return status code
**/
static bool
create_socket_events(const int epfd, endpoint* eps, const int ep_cnt)
{
  struct epoll_event ev;
  int i;

  /* Add all sockets to the event queue. The auxiliary data pointer should
   * point at the endpoint structure, so that all relevant data can be
   * accessed when the event is triggered. */
  for (i = 0; i < ep_cnt; i++) {
    ev.events = EPOLLIN;
    ev.data.ptr = &eps[i];

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, eps[i].ep_sock, &ev) == -1) {
      warn("Unable to add a socket to the event queue");
      return false;
    }
  }

  return true;
}

/** Create a new signal file descriptor and add it to the event queue.
 *
 * @param[out] sigfd signal file descriptor
 * @param[in]  epfd  epoll(2) file descriptor
 * @param[in]  opts  command-line options
 *
 * @return status code
**/
static bool 
create_signal_event(int* sigfd, const int epfd, const sub_options* opts)
{
  struct epoll_event ev;
  sigset_t mask;

  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);    /* User-generated ^C interrupt. */
  sigaddset(&mask, SIGHUP);    /* Lost of a SSH connection.    */

  if (opts->so_tout > 0)
    sigaddset(&mask, SIGALRM); /* Process timeout.             */

  /* Prevent the above signals from asynchronous handling. */
  sigprocmask(SIG_BLOCK, &mask, NULL);

  /* Create a new signal file descriptor. */
  *sigfd = signalfd(-1, &mask, 0);
  if (*sigfd == -1) {
    warn("Unable to create signal file descriptor");
    return false;
  }

  /* Add the signal file descriptor to the event queue. */
  ev.events = EPOLLIN;
  ev.data.fd = *sigfd;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, *sigfd, &ev) == -1) {
    warn("Unable to add the signal file descriptor to the event queue");
    return false;
  }

  return true;
}

/** Print the payload content as a CSV-formatted line to the standard output.
 *
 * @param[in] pl    payload
 * @param[in] ep    connection endpoint
 * @param[in] tv    packet arrival time
 * @param[in] hname hostname
**/
static void
print_payload_csv(const payload* pl,
                  const endpoint* ep,
                  const struct timespec* tv,
                  const char* hname,
                  const sub_options* opts)
{
  struct in_addr maddr;

  maddr.s_addr = pl->pl_maddr;
  printf("%u,%u,%s,%u,%.*s,%.*s,%.*s,%.*s,"
         "%" PRIu64 ".%.9" PRIu32 ",%ld.%.9ld\n",
    pl->pl_snum,
    pl->pl_sid,
    inet_ntoa(maddr),
    opts->so_port,
    (int)sizeof(pl->pl_iname), pl->pl_iname,
    (int)sizeof(pl->pl_hname), pl->pl_hname,
    (int)sizeof(ep->ep_iname), ep->ep_iname,
    HNAME_LEN, hname,
    pl->pl_sec,
    pl->pl_nsec,
    tv->tv_sec,
    tv->tv_nsec);
}

/** Print the payload content in the raw binary format (big-endian) to the
 * standard output.
 *
 * @param[in] pl    payload
 * @param[in] ep    connection endpoint
 * @param[in] tv    packet arrival time
 * @param[in] hname hostname
 * @param[in] opts  command-line options
**/
static void
print_payload_raw(const payload* pl,
                  const endpoint* ep,
                  const struct timespec* tv,
                  const char* hname,
                  const sub_options* opts)
{
  raw_output ro;

  ro.ro_pl.pl_fver  = pl->pl_fver;
  ro.ro_pl.pl_snum  = pl->pl_snum;
  ro.ro_pl.pl_sid   = pl->pl_sid;
  ro.ro_pl.pl_maddr = pl->pl_maddr;
  ro.ro_pl.pl_mport = opts->so_port;
  memcpy(ro.ro_pl.pl_iname, pl->pl_iname, sizeof(pl->pl_iname));
  memcpy(ro.ro_pl.pl_hname, pl->pl_hname, sizeof(pl->pl_hname));
  ro.ro_pl.pl_sec   = pl->pl_sec;
  ro.ro_pl.pl_nsec  = pl->pl_nsec;
  memcpy(ro.ro_iname, ep->ep_iname, sizeof(ep->ep_iname));
  memcpy(ro.ro_hname, hname, HNAME_LEN);
  ro.ro_sec         = (uint64_t)tv->tv_sec;
  ro.ro_nsec        = (uint32_t)tv->tv_nsec;

  fwrite(&ro, sizeof(ro), 1, stdout);
}

/** Convert all integers from the big-endian to host byte order.
 *
 * @param[in] pl payload
**/
static void
convert_payload(payload* pl)
{
  pl->pl_fver  = ntohs(pl->pl_fver);
  pl->pl_mport = ntohs(pl->pl_mport);
  pl->pl_maddr = ntohl(pl->pl_maddr);
  pl->pl_snum  = ntohl(pl->pl_snum);
  pl->pl_sid   = ntohl(pl->pl_sid);
  pl->pl_sec   = ntohll(pl->pl_sec);
  pl->pl_nsec  = ntohl(pl->pl_nsec);
}

/** Read all incoming datagrams associated with an endpoint.
 *
 * @param[out] nrecv overall number of received datagrams
 * @param[in]  ep    endpoint
 * @param[in]  hname hostname
 * @param[in]  opts  command-line options
 *
 * @return status code
**/
static bool
handle_event(uint32_t* nrecv,
             endpoint* ep,
             const char* hname,
             const sub_options* opts)
{
  payload pl;
  ssize_t nbytes;
  struct sockaddr_in addr;
  socklen_t addr_len;
  struct timespec tv;

  /* Prepare the address for the ingress loop. */
  addr.sin_port   = htons((uint16_t)opts->so_port);
  addr.sin_family = AF_INET;

  /* Loop through all available datagrams on the socket. */
  while (1) {
    /* Read an incoming datagram. */
    addr_len = (socklen_t)sizeof(struct sockaddr_in*);
    nbytes = recvfrom(ep->ep_sock,                         // socket
                      &pl, sizeof(pl),                     // payload
                      MSG_TRUNC | MSG_DONTWAIT,            // flags
                      (struct sockaddr*)&addr, &addr_len); // address
    if (nbytes == -1) {
      /* Exit the reading loop if there are no more datagrams to process. */
      if (errno == EAGAIN)
        break;

      /* Otherwise register the error with the user. */
      warn("Unable to receive datagram");
      return false;
    }

    /* Verify the size of the received payload. */
    if ((size_t)nbytes != sizeof(pl)) {
      warnx("Wrong payload size, expected: %zu, got: %zd", sizeof(pl), nbytes);
      continue;
    }

    convert_payload(&pl);

    /* Ensure that the format version is supported. */
    if (pl.pl_fver != PAYLOAD_VERSION) {
      warnx("Unsupported payload version, expected: %u, got: %u",
            PAYLOAD_VERSION, pl.pl_fver);
      continue;
    }

    /* Print the contents of the payload in the selected format. */
    if ((opts->so_sid == 0 || opts->so_sid == pl.pl_sid)
      && pl.pl_snum >= opts->so_off) {

      clock_gettime(CLOCK_REALTIME, &tv);

      /* Apply the sequence number offset. */
      pl.pl_snum -= opts->so_off;

      if (opts->so_raw)
        print_payload_raw(&pl, ep, &tv, hname, opts);
      else
        print_payload_csv(&pl, ep, &tv, hname, opts);
    }

    /* Successfully received a datagram. */
    (*nrecv)++;
    if (*nrecv == opts->so_exp)
      break;
  }

  return true;
}

/** Receive datagrams on all initialized connections.
 *
 * @param[in] epfd  epoll(2) file descriptor
 * @param[in] sigfd signal file descriptor
 * @param[in] hname hostname
 * @param[in] opts  command-line options
 *
 * @return status code
**/
static bool
receive_datagrams(const int epfd,
                  const int sigfd,
                  const char* hname,
                  const sub_options* opts)
{
  struct epoll_event evs[64];
  uint32_t nrecv;
  int ev_cnt;
  int i;

  nrecv = 0;

  /* Print the CSV header. */
  if (!opts->so_raw)
    printf("SequenceNum,SessionID,MulticastAddr,MulticastPort,PubInterface,"
           "PubHostname,SubInterface,SubHostname,TimeOfDeparture,"
           "TimeOfArrival\n");

  /* Receive datagrams on all initialized connections. */
  while (1) {
    ev_cnt = epoll_wait(epfd, evs, 64, -1);
    if (ev_cnt < 0) {
      warn("Event queue reading failed");
      return false;
    }

    /* Handle each event. */
    for (i = 0; i < ev_cnt; i++) {
      /* Handle the signal event for SIGINT, SIGHUP and optionally SIGALRM. */
      if (evs[i].data.fd == sigfd)
        return true;

      /* Handle socket events. */
      if (!handle_event(&nrecv, evs[i].data.ptr, hname, opts))
        return false;

      /* Quit after expected number of received datagrams. */
      if (opts->so_exp != 0 && nrecv == opts->so_exp)
        return true;
    }
  }

  return true;
}

/** Install signal alarm with the user-selected millisecond precision.
 *
 * @param[in] opts command-line options
 *
 * @return status code
**/
static bool
install_alarm(const sub_options* opts)
{
  struct itimerspec spec;
  timer_t tm;

  /* Zero denotes situation where no timeout is specified. */
  if (opts->so_tout == 0)
    return true;

  convert_millis(&spec.it_value, opts->so_tout);
  convert_millis(&spec.it_interval, 0);

  /* Create and arm the timer based on the selected timeout. */
  if (timer_create(CLOCK_REALTIME, NULL, &tm) == -1) {
    warn("Unable to create timer");
    return false;
  }

  if (timer_settime(tm, 0, &spec, NULL) == -1) {
    warn("Unable to set the timer");
    return false;
  }

  return true;
}

/** Disable the standard output stream buffering based on user settings.
 *
 * @param[in] opts command-line options
 *
 * @return status code
**/
static bool
disable_buffering(const sub_options* opts)
{
  if (opts->so_unb == 0)
    return true;

  if (setvbuf(stdout, NULL, _IONBF, 0) != 0) {
    warn("Unable to disable stdio buffering");
    return false;
  }

  return true;
}

/** Multicast heartbeat subscriber. */
int
main(int argc, char* argv[])
{
  /* Command-line options. */
  sub_options opts;

  /* Endpoint array. */
  endpoint* eps;
  int ep_cnt;
  int ep_idx;

  /* Signal and event management. */
  int epfd;
  int sigfd;

  /* Cached hostname. */
  char hname[HNAME_LEN+1];

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

  /* Disable buffering on the standard output. */
  if (!disable_buffering(&opts))
    return EXIT_FAILURE;

  /* Parse and validate endpoints. */
  if (!parse_endpoints(eps, ep_idx, argv, ep_cnt))
    return EXIT_FAILURE;

  /* Create the event queue. */
  if (!create_event_queue(&epfd))
    return EXIT_FAILURE;

  /* Initialise the sockets based on selected interfaces. */
  if (!create_sockets(eps, ep_cnt, &opts))
    return EXIT_FAILURE;

  /* Create the socket events and add them to the event queue. */
  if (!create_socket_events(epfd, eps, ep_cnt))
    return EXIT_FAILURE;

  /* Create a signal event and add it to the event queue. */
  if (!create_signal_event(&sigfd, epfd, &opts))
    return EXIT_FAILURE;

  /* Install the signal alarm. */
  if (!install_alarm(&opts))
    return EXIT_FAILURE;

  /* Start receiving datagrams. */
  if (!receive_datagrams(epfd, sigfd, hname, &opts))
    return EXIT_FAILURE;

  /* Flush the standard output stream. */
  fflush(stdout);
  free(eps);

  return EXIT_SUCCESS;
}
