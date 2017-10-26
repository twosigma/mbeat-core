// Copyright (c) 2017 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

// Select the appropriate event queue.
#if defined(__linux__)
  #include <sys/epoll.h>
  #include <sys/signalfd.h>
#elif defined(__FreeBSD__)
  #include <sys/event.h>
#else
  #error "System not supported."
#endif

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
#include "parse.h"


// Default values for optional arguments.
#define DEF_BUFFER_SIZE 0 // Zero denotes the system default.
#define DEF_SESSION_ID  0 // Zero denotes no session ID filtering.
#define DEF_OFFSET      0 // Sequence numbers have no offset by default.
#define DEF_ERROR       0 // Do not stop the process on receiving error.
#define DEF_RAW_OUTPUT  0 // Raw binary output is disabled by default.
#define DEF_UNBUFFERED  0 // Unbuffered output is disabled by default.
#define DEF_NOTIFY_LEVEL 1 // Log errors and warnings by default.

// Command-line options.
static uint64_t opbuf;  ///< Socket receive buffer size in bytes.
static uint64_t opsid;  ///< Session ID filter of received datagrams.
static uint64_t opoff;  ///< Sequence number offset.
static uint64_t opport; ///< UDP port for all endpoints.
static uint8_t  operr;  ///< Process exit policy on receiving error.
static uint8_t  opraw;  ///< Output received datagrams in raw binary format.
static uint8_t  opunb;  ///< Turn off buffering on the output stream.
static uint8_t  opnlvl; ///< Notification verbosity level.

// Signal and event management.
static int eqfd;
static int sigfd;

/// Print the utility usage information to the standard output.
static void
print_usage(void)
{
  fprintf(stderr,
    "Multicast heartbeat subscriber - v%d.%d.%d\n"
    "Receive datagrams from selected network endpoints.\n\n"

    "Usage:\n"
    "  msub [OPTIONS] iface=maddr [iface=maddr ...]\n\n"

    "Options:\n"
    "  -b BSZ  Receive buffer size in bytes.\n"
    "  -e      Stop the process on receiving error.\n"
    "  -h      Print this help message.\n"
    "  -o OFF  Ignore payloads with lesser sequence number. (def=%d)\n"
    "  -p NUM  UDP port for all endpoints. (def=%d)\n"
    "  -r      Output the data in raw binary format.\n"
    "  -s SID  Only report datagrams with this session ID.\n"
    "  -u      Disable output buffering.\n",
    MBEAT_VERSION_MAJOR,
    MBEAT_VERSION_MINOR,
    MBEAT_VERSION_PATCH,
    DEF_OFFSET,
    MBEAT_PORT);
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
  opsid  = DEF_SESSION_ID;
  opoff  = DEF_OFFSET;
  opport = MBEAT_PORT;
  operr  = DEF_ERROR;
  opraw  = DEF_RAW_OUTPUT;
  opunb  = DEF_UNBUFFERED;
  opnlvl = DEF_NOTIFY_LEVEL;

  while ((opt = getopt(argc, argv, "b:e:ho:p:rs:uv")) != -1) {
    switch (opt) {

      // Receive buffer size.
      case 'b':
        if (parse_uint64(&opbuf, optarg, 0, UINT64_MAX) == 0)
          return false;
        break;

      // Process exit on receiving error.
      case 'e':
        operr = 1;
        break;

      // Usage information.
      case 'h':
        print_usage();
        return false;

      // Sequence number offset.
      case 'o':
        if (parse_uint64(&opoff, optarg, 1, UINT64_MAX) == 0)
          return false;
        break;

      // UDP port for all endpoints.
      case 'p':
        if (parse_uint64(&opport, optarg, 0, 65535) == 0)
          return false;
        break;

      // Raw binary output option.
      case 'r':
        opraw = 1;
        break;

      // Session ID of the current run.
      case 's':
        if (parse_uint64(&opsid, optarg, 1, UINT64_MAX) == 0)
          return false;
        break;

      // Unbuffered output option.
      case 'u':
        opunb = 1;
        break;

      // Logging verbosity level.
      case 'v':
        if (opnlvl < NL_TRACE)
          opnlvl++;
        break;

      // Unknown option.
      case '?':
        fprintf(stderr, "Invalid option '%c'", optopt);
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
  int buf_size;
  struct sockaddr_in addr;
  struct ip_mreq req;
  char* mcast_str;
  endpoint* ep;

  enable = 1;
  for (ep = eps; ep != NULL ; ep = ep->ep_next) {
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

    // Request the Time-To-Live property of each incoming datagram.
    if (setsockopt(ep->ep_sock, IPPROTO_IP, IP_RECVTTL,
                   &enable, sizeof(enable)) == -1)
      notify(NL_WARN, true, "Unable to request Time-To-Live information");

    // Set the socket receive buffer size to the requested value.
    if (opbuf != 0) {
      buf_size = (int)opbuf;
      if (setsockopt(ep->ep_sock, SOL_SOCKET, SO_RCVBUF,
                     &buf_size, sizeof(buf_size)) == -1) {
        notify(NL_ERROR, true,
               "Unable to set the socket receive buffer size to %d", buf_size);
        return false;
      }
    }

    mcast_str = inet_ntoa(ep->ep_maddr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)opport);
    addr.sin_addr   = ep->ep_maddr;

    // Bind the socket to the multicast group.
    if (bind(ep->ep_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
      notify(NL_ERROR, true, "Unable to bind to address %s and port %" PRIu64,
             mcast_str, opport);
      return false;
    }

    // Subscribe the socket to the multicast group.
    req.imr_interface.s_addr = ep->ep_iaddr.s_addr;
    req.imr_multiaddr.s_addr = ep->ep_maddr.s_addr;
    if (setsockopt(ep->ep_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &req, sizeof(req)) == -1) {
      notify(NL_ERROR, true, "Unable to join multicast group %s", mcast_str);
      return false;
    }
  }

  return true;
}

/// Create the event queue.
/// @return status code
static bool
create_event_queue(void)
{
  #if defined(__linux__)
    eqfd = epoll_create(ENDPOINT_MAX);
  #endif

  #if defined(__FreeBSD__)
    eqfd = kqueue();
  #endif

  if (eqfd < 0) {
    notify(NL_ERROR, true, "Unable to create event queue");
    return false;
  }

  return true;
}

/// Add the socket associated with each endpoint to the event queue.
/// @return status code
///
/// @param[in] eps  endpoint list
static bool
create_socket_events(endpoint* eps)
{
  endpoint* ep;

  #if defined(__linux__)
    struct epoll_event ev;
  #endif

  #if defined(__FreeBSD__)
    struct kevent ev;
  #endif

  // Add all sockets to the event queue. The auxiliary data pointer should
  // point at the endpoint structure, so that all relevant data can be
  // accessed when the event is triggered.
  for (ep = eps; ep != NULL; ep = ep->ep_next) {
    #if defined(__linux__)
      ev.events = EPOLLIN;
      ev.data.ptr = ep;

      if (epoll_ctl(eqfd, EPOLL_CTL_ADD, ep->ep_sock, &ev) == -1) {
        notify(NL_ERROR, true, "Unable to add a socket to the event queue");
        return false;
      }
    #endif

    #if defined(__FreeBSD__)
      EV_SET(&ev, ep->ep_sock, EVFILT_READ, EV_ADD, 0, 0, ep);
      if (kevent(eqfd, &ev, 1, NULL, 0, NULL) == -1) {
        notify(NL_ERROR, true, "Unable to add a socket to the event queue");
        return false;
      }
    #endif
  }

  return true;
}

/// Create a new signal file descriptor and add it to the event queue.
/// @return status code
static bool
create_signal_event(void)
{
  sigset_t mask;

  #if defined(__linux__)
    struct epoll_event ev;
  #endif

  #if defined(__FreeBSD__)
    struct kevent ev;
  #endif

  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);    // User-generated ^C interrupt.
  sigaddset(&mask, SIGHUP);    // Loss of a SSH connection.

  // Prevent the above signals from asynchronous handling.
  sigprocmask(SIG_BLOCK, &mask, NULL);

  #if defined(__linux__)
    // Create a new signal file descriptor.
    sigfd = signalfd(-1, &mask, 0);
    if (sigfd == -1) {
      notify(NL_ERROR, true, "Unable to create signal file descriptor");
      return false;
    }

    // Add the signal file descriptor to the event queue.
    ev.events = EPOLLIN;
    ev.data.fd = sigfd;
    if (epoll_ctl(eqfd, EPOLL_CTL_ADD, sigfd, &ev) == -1) {
      notify(NL_ERROR, false,
             "Unable to add the signal file descriptor to the event queue");
      return false;
    }
  #endif

  #if defined(__FreeBSD__)
    // Avoid unused variable warning.
    (void)sigfd;

    // Add SIGINT to the event queue.
    EV_SET(&ev, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
    if (kevent(eqfd, &ev, 1, NULL, 0, NULL) == -1) {
      notify(NL_ERROR, true, "Unable to add SIGINT to the event queue");
      return false;
    }

    // Add SIGHUP to the event queue.
    EV_SET(&ev, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
    if (kevent(eqfd, &ev, 1, NULL, 0, NULL) == -1) {
      notify(NL_ERROR, true, "Unable to add SIGHUP to the event queue");
      return false;
    }
  #endif

  return true;
}

/// Print the payload content as a CSV-formatted line to the standard output.
///
/// @param[in] pl  payload
/// @param[in] ep  connection endpoint
/// @param[in] tv  packet arrival time
/// @param[in] ttl Time-To-Live value upon arrival
static void
print_payload_csv(const payload* pl,
                  const endpoint* ep,
                  const struct timespec* tv,
                  const int ttl)
{
  char ttl_str[8];
  uint32_t dep;
  uint32_t arr;

  // Destination Time-To-Live string, depending on it's availability.
  if (0 <= ttl && ttl <= 255)
    snprintf(ttl_str, sizeof(ttl_str), "%d", ttl);
  else
    strcpy(ttl_str, "N/A");

  // Round the nanosecond time parts of departure and arrival to 3 digits.
  dep = pl->pl_nsec / (uint32_t)1000000;
  arr = (uint32_t)tv->tv_nsec / (uint32_t)1000000;

  printf("%" PRIu64 ","                 // SID
         "%" PRIu64 ","                 // SeqNum
         "%" PRIu64 ","                 // SeqLen
         "%s,"                          // McastAddr
         "%" PRIu16 ","                 // McastPort
         "%" PRIu8  ","                 // SrcTTL
         "%s,"                          // DstTTL
         "%.*s,"                        // PubIf
         "%.*s,"                        // PubHost
         "%.*s,"                        // SubIf
         "%.*s,"                        // SubHost
         "%" PRIu64 ".%.3" PRIu32 ","   // TimeOfDep
         "%" PRIu64 ".%.3" PRIu32 "\n", // TimeOfArr
    pl->pl_sid,
    pl->pl_snum,
    pl->pl_slen,
    inet_ntoa(ep->ep_maddr),
    pl->pl_mport,
    pl->pl_ttl,
    ttl_str,
    (int)sizeof(pl->pl_iname), pl->pl_iname,
    (int)sizeof(pl->pl_hname), pl->pl_hname,
    (int)sizeof(ep->ep_iname), ep->ep_iname,
    (int)sizeof(hname), hname,
    pl->pl_sec,
    dep,
    (uint64_t)tv->tv_sec,
    arr);
}

/// Print the payload content in the raw binary format (big-endian) to the
/// standard output.
///
/// @param[in] pl  payload
/// @param[in] ep  connection endpoint
/// @param[in] tv  packet arrival time
/// @param[in] ttl Time-To-Live value upon arrival
static void
print_payload_raw(const payload* pl,
                  const endpoint* ep,
                  const struct timespec* tv,
                  const int ttl)
{
  raw_output ro;

  memcpy(&ro.ro_pl, pl, sizeof(*pl));
  memcpy(ro.ro_iname, ep->ep_iname, sizeof(ep->ep_iname));
  memcpy(ro.ro_hname, hname, sizeof(hname));
  ro.ro_sec  = ((uint64_t)tv->tv_sec);
  ro.ro_nsec = ((uint32_t)tv->tv_nsec);
  ro.ro_ttla = (0 <= ttl && ttl <= 255) ? 1 : 0;
  ro.ro_ttl  = (uint8_t)ttl;
  memset(ro.ro_pad, 0, sizeof(ro.ro_pad));

  fwrite(&ro, sizeof(ro), 1, stdout);
}

/// Determine whether to print the payload and choose the method based on the
/// user-selected options.
///
/// @param[in] pl  payload
/// @param[in] ep  endpoint
/// @param[in] ttl Time-To-Live value upon arrival
static void
print_payload(payload* pl, const endpoint* ep, const int ttl)
{
  struct timespec tv;

  // Filter out non-matching session IDs.
  if (opsid != 0 && opsid != pl->pl_sid)
    return;

  // Filter out payloads below the offset threshold.
  if (opoff > pl->pl_snum)
    return;

  // Apply the sequence number offset.
  (*pl).pl_snum -= opoff;

  clock_gettime(CLOCK_REALTIME, &tv);

  // Perform the user-selected type of output.
  if (opraw)
    print_payload_raw(pl, ep, &tv, ttl);
  else
    print_payload_csv(pl, ep, &tv, ttl);
}

/// Convert all integers from the network to host byte order.
///
/// @param[in] pl payload
static void
convert_payload(payload* pl)
{
  pl->pl_magic = ntohl(pl->pl_magic);
  pl->pl_mport = ntohs(pl->pl_mport);
  pl->pl_maddr = ntohl(pl->pl_maddr);
  pl->pl_sid   = ntohll(pl->pl_sid);
  pl->pl_snum  = ntohll(pl->pl_snum);
  pl->pl_slen  = ntohll(pl->pl_slen);
  pl->pl_sec   = ntohll(pl->pl_sec);
  pl->pl_nsec  = ntohl(pl->pl_nsec);
}

/// Traverse the control messages and obtain the received Time-To-Live value.
/// @return status code
///
/// @param[out] ttl Time-To-Live value
/// @param[in]  msg received message
static bool
retrieve_ttl(int* ttl, struct msghdr* msg)
{
  struct cmsghdr* cmsg;
  int type;

  #if defined(__linux__)
    type = IP_TTL;
  #endif

  #if defined(__FreeBSD__)
    type = IP_RECVTTL;
  #endif

  notify(NL_TRACE, false, "Retrieving the Time-To-Live data");

  for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)) {
    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == type) {
      *ttl = *(int*)CMSG_DATA(cmsg);
      return true;
    }
  }

  notify(NL_WARN, false, "Unable to retrieve the Time-To-Live data");

  *ttl = -1;
  return false;
}

/// Read all incoming datagrams associated with an endpoint.
/// @return status code
///
/// @param[in] ep endpoint
static bool
handle_event(endpoint* ep)
{
  payload pl;
  int ttl;
  ssize_t nbytes;
  struct sockaddr_in addr;
  struct msghdr msg;
  struct iovec data;
  char cdata[128];

  // Prepare the address for the ingress loop.
  addr.sin_port   = htons((uint16_t)opport);
  addr.sin_family = AF_INET;

  // Loop through all available datagrams on the socket.
  while (1) {
    // Prepare payload data.
    data.iov_base = &pl;
    data.iov_len  = sizeof(pl);

    // Prepare the message.
    msg.msg_name       = &addr;
    msg.msg_namelen    = sizeof(addr);
    msg.msg_iov        = &data;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cdata;
    msg.msg_controllen = sizeof(cdata);

    // Read an incoming datagram.
    nbytes = recvmsg(ep->ep_sock, &msg, MSG_TRUNC | MSG_DONTWAIT);
    if (nbytes == -1) {
      // Exit the reading loop if there are no more datagrams to process.
      if (errno == EAGAIN)
        break;

      // Otherwise register the error with the user.
      notify(operr ? NL_ERROR : NL_WARN, true,
             "Unable to receive datagram on interface %s "
             "from multicast group %s", ep->ep_iname, inet_ntoa(ep->ep_maddr));

      if (operr)
        return false;
    }

    // Verify the size of the received payload.
    if ((size_t)nbytes != sizeof(pl)) {
      notify(NL_WARN, false, "Wrong payload size, expected: %zu, got: %zd",
             sizeof(pl), nbytes);
      continue;
    }

    retrieve_ttl(&ttl, &msg);

    convert_payload(&pl);

    // Verify the magic number of the payload.
    if (pl.pl_magic != MBEAT_PAYLOAD_MAGIC) {
      notify(NL_WARN, false,
             "Payload magic number invalid, expected: %u, got: %u",
             MBEAT_PAYLOAD_MAGIC, pl.pl_magic);
      continue;
    }

    // Ensure that the format version is supported.
    if (pl.pl_fver != MBEAT_PAYLOAD_VERSION) {
      notify(NL_WARN, false, 
             "Unsupported payload version, expected: %u, got: %u",
             MBEAT_PAYLOAD_VERSION, pl.pl_fver);
      continue;
    }

    print_payload(&pl, ep, ttl);
  }

  return true;
}

/// Receive datagrams on all initialized connections.
/// @return status code
static bool
receive_datagrams(void)
{
  int ev_cnt;
  int i;

  #if defined(__linux__)
    struct epoll_event evs[64];
  #endif

  #if defined(__FreeBSD__)
    struct kevent evs[64];
  #endif

  // Print the CSV header.
  if (!opraw)
    printf("SID,SeqNum,SeqLen,McastAddr,McastPort,SrcTTL,DstTTL,PubIf,PubHost,"
           "SubIf,SubHost,TimeDep,TimeArr\n");

  // Receive datagrams on all initialized connections.
  while (1) {
    notify(NL_TRACE, false, "Waiting for incoming events");

    #if defined(__linux__)
      ev_cnt = epoll_wait(eqfd, evs, 64, -1);
    #endif

    #if defined(__FreeBSD__)
      ev_cnt = kevent(eqfd, NULL, 0, evs, 64, NULL);
    #endif

    if (ev_cnt < 0) {
      notify(NL_ERROR, true, "Event queue reading failed");
      return false;
    }

    notify(NL_DEBUG, false, "%d events", ev_cnt);

    // Handle each event.
    for (i = 0; i < ev_cnt; i++) {

      #if defined(__linux__)
        // Handle the signal event for SIGINT and SIGHUP.
        if (evs[i].data.fd == sigfd)
          return true;

        // Handle socket events.
        if (!handle_event(evs[i].data.ptr))
          return false;
      #endif

      #if defined(__FreeBSD__)
        // Avoid unused variable warning.
        (void)sigfd;

        // Handle the signal event for SIGINT and SIGHUP.
        if (evs[i].filter == EVFILT_SIGNAL)
          return true;

        // Handle socket events.
        if (!handle_event(evs[i].udata))
          return false;
      #endif
    }
  }

  return true;
}

/// Disable the standard output stream buffering based on user settings.
static void 
disable_buffering(void)
{
  if (opunb == 0)
    return;

  notify(NL_DEBUG, false, "Disabling stdio buffering");
  if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
    notify(NL_WARN, true, "Unable to disable stdio buffering");
}

/// Multicast heartbeat subscriber.
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

  // Disable buffering on the standard output.
  disable_buffering();

  // Parse and validate endpoints.
  if (!parse_endpoints(&eps, ep_idx, argv, ep_cnt))
    return EXIT_FAILURE;

  // Create the event queue.
  if (!create_event_queue())
    return EXIT_FAILURE;

  // Initialise the sockets based on selected interfaces.
  if (!create_sockets(eps))
    return EXIT_FAILURE;

  // Create the socket events and add them to the event queue.
  if (!create_socket_events(eps))
    return EXIT_FAILURE;

  // Create a signal event and add it to the event queue.
  if (!create_signal_event())
    return EXIT_FAILURE;

  // Start receiving datagrams.
  if (!receive_datagrams())
    return EXIT_FAILURE;

  fflush(stdout);
  free_endpoints(eps);

  return EXIT_SUCCESS;
}
