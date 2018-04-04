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
#include <getopt.h>

#include "types.h"
#include "common.h"
#include "parse.h"
#include "sub.h"


// Default values for optional arguments.
#define DEF_BUFFER_SIZE  0 // Zero denotes the system default.
#define DEF_KEY          0 // Zero denotes no key filtering.
#define DEF_OFFSET       0 // Sequence numbers have no offset by default.
#define DEF_ERROR        0 // Do not stop the process on receiving error.
#define DEF_RAW_OUTPUT   0 // Raw binary output is disabled by default.
#define DEF_UNBUFFERED   0 // Unbuffered output is disabled by default.
#define DEF_NOTIFY_LEVEL 1 // Log errors and warnings by default.
#define DEF_NOTIFY_COLOR 1 // Colors in the notification output.

// Command-line options.
static uint64_t op_buf;  ///< Socket receive buffer size in bytes.
static uint64_t op_key;  ///< Key filter of received datagrams.
static uint64_t op_off;  ///< Sequence number offset.
static uint64_t op_port; ///< UDP port for all endpoints.
static uint8_t  op_err;  ///< Process exit policy on receiving error.
static uint8_t  op_raw;  ///< Output received datagrams in raw binary format.
static uint8_t  op_unb;  ///< Turn off buffering on the output stream.
static uint8_t  op_nlvl; ///< Notification verbosity level.
static uint8_t  op_ncol; ///< Notification coloring policy.

// Object lists.
static endpoint* eps;

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
    "  -b, --buffer-size BSZ      Receive buffer size in bytes.\n"
    "  -e, --exit-on-error        Stop the process on receiving error.\n"
    "  -h, --help                 Print this help message.\n"
    "  -k, --key KEY              Only report datagrams with this key.\n"
    "  -n, --no-color             Turn off colors in logging messages.\n"
    "  -o, --offset OFF           Ignore payloads with lesser sequence number."
      " (def=%d)\n"
    "  -p, --port NUM             UDP port for all endpoints. (def=%d)\n"
    "  -r, --raw-output           Output the data in raw binary format.\n"
    "  -u, --disable-buffering    Disable output buffering.\n"
    "  -v, --verbose              Increase the logging verbosity.\n",
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
  struct option lopts[] = {
    {"buffer-size",       required_argument, NULL, 'b'},
    {"exit-on-error",     no_argument,       NULL, 'e'},
    {"help",              no_argument,       NULL, 'h'},
    {"no-color",          no_argument,       NULL, 'n'},
    {"offset",            required_argument, NULL, 'o'},
    {"port",              required_argument, NULL, 'p'},
    {"raw-output",        no_argument,       NULL, 'r'},
    {"disable-buffering", no_argument,       NULL, 'u'},
    {"verbose",           no_argument,       NULL, 'v'},
    {NULL, 0, NULL, 0}
  };

  // Set optional arguments to sensible defaults.
  op_buf  = DEF_BUFFER_SIZE;
  op_key  = DEF_KEY;
  op_off  = DEF_OFFSET;
  op_port = MBEAT_PORT;
  op_err  = DEF_ERROR;
  op_raw  = DEF_RAW_OUTPUT;
  op_unb  = DEF_UNBUFFERED;
  op_nlvl = nlvl = DEF_NOTIFY_LEVEL;
  op_ncol = ncol = DEF_NOTIFY_COLOR;

  while ((opt = getopt_long(argc, argv, "b:ehk:no:p:ruv", lopts, NULL)) != -1) {
    switch (opt) {

      // Receive buffer size.
      case 'b':
        if (parse_scalar(&op_buf, optarg, parse_memory_unit) == 0)
          return false;
        break;

      // Process exit on receiving error.
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

      // Turn off the notification coloring.
      case 'n':
        op_ncol = 0;
        break;

      // Sequence number offset.
      case 'o':
        if (parse_uint64(&op_off, optarg, 1, UINT64_MAX) == 0)
          return false;
        break;

      // UDP port for all endpoints.
      case 'p':
        if (parse_uint64(&op_port, optarg, 0, 65535) == 0)
          return false;
        break;

      // Raw binary output option.
      case 'r':
        op_raw = 1;
        break;

      // Unbuffered output option.
      case 'u':
        op_unb = 1;
        break;

      // Logging verbosity level.
      case 'v':
        if (op_nlvl < NL_TRACE)
          op_nlvl++;
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
  nlvl = op_nlvl;
  ncol = op_ncol;

  *ep_cnt = argc - optind;
  *ep_idx = optind;

  return true;
}

/// Create endpoint sockets and apply the interface settings.
/// @return status code
static bool
create_sockets(void)
{
  int enable;
  int buf_size;
  struct sockaddr_in addr;
  struct ip_mreq req;
  char* mcast_str;
  endpoint* ep;

  enable = 1;
  for (ep = eps; ep != NULL ; ep = ep->ep_next) {
    notify(NL_TRACE, false,
           "Creating endpoint on interface %s for multicast group %s",
           ep->ep_iname, inet_ntoa(ep->ep_maddr));

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
    if (op_buf != 0) {
      buf_size = (int)op_buf;
      if (setsockopt(ep->ep_sock, SOL_SOCKET, SO_RCVBUF,
                     &buf_size, sizeof(buf_size)) == -1) {
        notify(NL_ERROR, true,
               "Unable to set the socket receive buffer size to %d", buf_size);
        return false;
      }
    }

    mcast_str = inet_ntoa(ep->ep_maddr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)op_port);
    addr.sin_addr   = ep->ep_maddr;

    // Bind the socket to the multicast group.
    if (bind(ep->ep_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
      notify(NL_ERROR, true, "Unable to bind to address %s and port %" PRIu64,
             mcast_str, op_port);
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

/// Add the socket associated with each endpoint to the event queue.
/// @return status code
static bool
add_socket_events(void)
{
  endpoint* ep;

  for (ep = eps; ep != NULL; ep = ep->ep_next)
    if (add_socket_event(ep) == false)
      return false;

  return true;
}

/// Print the payload content as a CSV-formatted line to the standard output.
///
/// @param[in] pl  payload
/// @param[in] ep  connection endpoint
/// @param[in] rtv packet system arrival time
/// @param[in] mtv packet steady arrival time
/// @param[in] ttl Time-To-Live value upon arrival
static void
print_payload_csv(const payload* pl,
                  const endpoint* ep,
                  const struct timespec* rtv,
                  const struct timespec* mtv,
                  const int ttl)
{
  uint64_t rtime;
  uint64_t mtime;
  char ttl_str[8];

  // Destination Time-To-Live string, depending on it's availability.
  if (0 <= ttl && ttl <= 255)
    snprintf(ttl_str, sizeof(ttl_str), "%d", ttl);
  else
    strcpy(ttl_str, "N/A");

  to_nanos(&rtime, *rtv);
  to_nanos(&mtime, *mtv);

  printf("%" PRIu64 ","   // Key
         "%" PRIu64 ","   // SeqNum
         "%" PRIu64 ","   // SeqLen
         "%s,"            // McastAddr
         "%" PRIu16 ","   // McastPort
         "%" PRIu8  ","   // SrcTTL
         "%s,"            // DstTTL
         "%.*s,"          // PubIf
         "%.*s,"          // PubHost
         "%.*s,"          // SubIf
         "%.*s,"          // SubHost
         "%" PRIu64 ","   // RealDep
         "%" PRIu64 ","   // RealArr
         "%" PRIu64 ","   // MonoDep
         "%" PRIu64 "\n", // MonoArr
    pl->pl_key,
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
    pl->pl_rtime, rtime,
    pl->pl_mtime, mtime);
}

/// Print the payload content in the raw binary format (big-endian) to the
/// standard output.
///
/// @param[in] pl  payload
/// @param[in] ep  connection endpoint
/// @param[in] rtv packet system arrival time
/// @param[in] mtv packet steady arrival time
/// @param[in] ttl Time-To-Live value upon arrival
static void
print_payload_raw(const payload* pl,
                  const endpoint* ep,
                  const struct timespec* rtv,
                  const struct timespec* mtv,
                  const int ttl)
{
  raw_output ro;

  memcpy(&ro.ro_pl, pl, sizeof(*pl));
  memcpy(ro.ro_iname, ep->ep_iname, sizeof(ep->ep_iname));
  memcpy(ro.ro_hname, hname, sizeof(hname));
  ro.ro_rtime = (uint64_t)rtv->tv_nsec
              + (1000000000ULL * (uint64_t)rtv->tv_sec);
  ro.ro_mtime = (uint64_t)mtv->tv_nsec
              + (1000000000ULL * (uint64_t)mtv->tv_sec);
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
  struct timespec rtv;
  struct timespec mtv;

  // Filter out non-matching keys.
  if (op_key != 0 && op_key != pl->pl_key)
    return;

  // Filter out payloads below the offset threshold.
  if (op_off > pl->pl_snum)
    return;

  // Apply the sequence number offset.
  (*pl).pl_snum -= op_off;

  // Get the system clock value.
  clock_gettime(CLOCK_REALTIME, &rtv);

  // Get the steady clock value.
  #ifdef __linux__
    clock_gettime(CLOCK_MONOTONIC_RAW, &mtv);
  #else
    clock_gettime(CLOCK_MONOTONIC, &mtv);
  #endif

  // Perform the user-selected type of output.
  if (op_raw)
    print_payload_raw(pl, ep, &rtv, &mtv, ttl);
  else
    print_payload_csv(pl, ep, &rtv, &mtv, ttl);
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
  pl->pl_key   = ntohll(pl->pl_key);
  pl->pl_snum  = ntohll(pl->pl_snum);
  pl->pl_slen  = ntohll(pl->pl_slen);
  pl->pl_rtime = ntohll(pl->pl_rtime);
  pl->pl_mtime = ntohll(pl->pl_mtime);
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

  #if defined(__FreeBSD__)
    type = IP_RECVTTL;
  #else
    type = IP_TTL;
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

/// Verify the payload suitability.
/// @return decision
///
/// @param[in] pl  payload
/// @param[in] nbs number of received bytes
static bool
verify_payload(const payload* pl, const ssize_t nbs)
{
  // Verify the size of the received payload.
  if ((size_t)nbs != sizeof(*pl)) {
    notify(NL_WARN, false, "Wrong payload size, expected: %zu, got: %zd",
           sizeof(pl), nbs);
    return false;
  }

  // Verify the magic number of the payload.
  if (pl->pl_magic != MBEAT_PAYLOAD_MAGIC) {
    notify(NL_WARN, false,
           "Payload magic number invalid, expected: %u, got: %u",
           MBEAT_PAYLOAD_MAGIC, pl->pl_magic);
    return false;
  }

  // Ensure that the format version is up-to-date.
  if (pl->pl_fver != MBEAT_PAYLOAD_VERSION) {
    notify(NL_WARN, false,
           "Unsupported payload version, expected: %u, got: %u",
           MBEAT_PAYLOAD_VERSION, pl->pl_fver);
    return false;
  }

  return true;
}

/// Read all incoming datagrams associated with an endpoint.
/// @return status code
///
/// @param[in] ep endpoint
bool
handle_event(endpoint* ep)
{
  payload pl;
  int ttl;
  ssize_t nbs;
  struct sockaddr_in addr;
  struct msghdr msg;
  struct iovec data;
  char cdata[128];

  // Prepare the address for the ingress loop.
  addr.sin_port   = htons((uint16_t)op_port);
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
    nbs = recvmsg(ep->ep_sock, &msg, MSG_TRUNC | MSG_DONTWAIT);
    if (nbs == -1) {
      // Exit the reading loop if there are no more datagrams to process.
      if (errno == EAGAIN)
        break;

      // Otherwise register the error with the user.
      notify(op_err ? NL_ERROR : NL_WARN, true,
             "Unable to receive datagram on interface %s "
             "from multicast group %s", ep->ep_iname, inet_ntoa(ep->ep_maddr));

      if (op_err)
        return false;
    }

    convert_payload(&pl);
    if (verify_payload(&pl, nbs) == false)
      continue;

    retrieve_ttl(&ttl, &msg);
    print_payload(&pl, ep, ttl);
  }

  return true;
}

/// Create a signal mask that will be used to allow/block process signals.
/// @return status code
///
/// @param[out] mask signal mask
bool
create_signal_mask(sigset_t* mask)
{
  // Clear the signal set.
  if (sigemptyset(mask) != 0) {
    notify(NL_ERROR, true, "Unable to clear the signal set");
    return false;
  }

  // Add the SIGINT signal to the set, to address the user-generated ^C
  // interrupts.
  if (sigaddset(mask, SIGINT) != 0) {
    notify(NL_ERROR, true, "Unable to add %s to the signal set", "SIGINT");
    return false;
  }

  // Add the SIGHUP signal to the set, to address the loss of a SSH connection.
  if (sigaddset(mask, SIGHUP) != 0) {
    notify(NL_ERROR, true, "Unable to add %s to the signal set", "SIGHUP");
    return false;
  }

  return true;
}

/// Print the CSV header.
static void
print_header(void)
{
  // No header is printed for the raw binary output.
  if (op_raw)
    return;

  printf("Key,SeqNum,SeqLen,"
         "McastAddr,McastPort,SrcTTL,DstTTL,"
         "PubIf,PubHost,SubIf,SubHost,"
         "RealDep,RealArr,MonoDep,MonoArr\n");
}

/// Disable the standard output stream buffering based on user settings.
static void 
disable_buffering(void)
{
  if (op_unb == 0)
    return;

  notify(NL_DEBUG, false, "Disabling stdio buffering");
  if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
    notify(NL_WARN, true, "Unable to disable stdio buffering");
}

/// Multicast heartbeat subscriber.
int
main(int argc, char* argv[])
{
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
  if (!create_sockets())
    return EXIT_FAILURE;

  // Create the socket events and add them to the event queue.
  if (!add_socket_events())
    return EXIT_FAILURE;

  // Create a signal event and add it to the event queue.
  if (!add_signal_events())
    return EXIT_FAILURE;

  // Print the CSV header to the standard output.
  print_header();

  // Start receiving datagrams.
  if (!receive_events(eps))
    return EXIT_FAILURE;

  fflush(stdout);
  free_endpoints(eps);

  return EXIT_SUCCESS;
}
