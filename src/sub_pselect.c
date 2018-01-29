// Copyright (c) 2017-2018 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#include "platform.h"

#if (MBEAT_EVENT == MBEAT_EVENT_PSELECT)

#include <sys/select.h>

#include <string.h>
#include <signal.h>
#include <errno.h>

#include "types.h"
#include "common.h"
#include "sub.h"


static fd_set eqfd;   ///< Event queue file descriptor.
static int nfds;      ///< Highest socket file descriptor number.
static bool sint;     ///< SIGINT occurrence flag.
static bool shup;     ///< SIGHUP occurrence flag.
static sigset_t mask; ///< Signal mask.

/// Trigger the signal flags based on the incoming signal.
///
/// @param[in] sig signal number
static void
signal_flags(int sig)
{
  if (sig == SIGINT)
    sint = true;

  if (sig == SIGHUP)
    shup = true;
}

/// Create the pselect event queue.
/// @return status code
bool
create_event_queue(void)
{
  notify(NL_DEBUG, false, "Using the %s event queue", "pselect");

  FD_ZERO(&eqfd);
  nfds = 0;
  sint = false;
  shup = false;

  return true;
}

/// Register a socket with the event queue.
/// @return status code
///
/// @param[in] ep endpoint
bool
add_socket_event(endpoint* ep)
{
  FD_SET(ep->ep_sock, &eqfd);

  // Increment the upper bound of socket numbers.
  if (ep->ep_sock > nfds)
    nfds = ep->ep_sock;

  return true;
}

/// Register events for signals SIGINT and SIGHUP.
/// @return status code
bool
add_signal_events(void)
{
  struct sigaction sa;

  sint = false;
  shup = false;

  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = signal_flags;

  // Install signal handler for SIGINT.
  if (sigaction(SIGINT, &sa, NULL) < 0) {
    notify(NL_ERROR, true, "Unable to add signal handler for %s", "SIGINT");
    return false;
  }

  // Install signal handler for SIGINT.
  if (sigaction(SIGHUP, &sa, NULL) < 0) {
    notify(NL_ERROR, true, "Unable to add signal handler for %s", "SIGHUP");
    return false;
  }

  return true;
}

/// Notify the user the type of the received signal.
static bool
report_signal(void)
{
  if (sint == true) {
    notify(NL_WARN, false, "Received the %s signal", SIGINT);
    return true;
  }

  if (shup == true) {
    notify(NL_WARN, false, "Received the %s signal", SIGHUP);
    return true;
  }

  notify(NL_ERROR, false, "Unknown signal received");
  return false;
}

/// Process the incoming network datagrams and process signals.
/// @return status code
///
/// @param[in] eps endpoints list
bool
receive_events(endpoint* eps)
{
  fd_set evs;
  int k;
  int i;
  endpoint* ep;
  int cnt;

  memcpy(&evs, &eqfd, sizeof(eqfd));
  cnt = pselect(nfds + 1, &evs, NULL, NULL, NULL, &mask);

  // Possible interruption by a signal.
  if (cnt == -1) {
    if (errno == EINTR)
      return report_signal();
    else {
      notify(NL_ERROR, true, "Problem while waiting for events");
      return false;
    }
  }

  k = 0;
  for (i = 0; i < cnt; i++) {
    // Skip to the next
    while (!FD_ISSET(k, &evs) && k < FD_SETSIZE)
      k++;

    // Check if the search was exhaustive.
    if (k == FD_SETSIZE)
      return true;

    // Find the corresponding endpoint object.
    for (ep = eps; ep != NULL; ep = ep->ep_next)
      if (ep->ep_sock == k)
        break;

    // Verify that a matching endpoint exists.
    if (ep == NULL) {
      notify(NL_WARN, false, "Unable to find endpoint with socket %d", k);
      return false;
    }

    // Handle socket events.
    if (!handle_event(ep))
      return false;
  }

  return true;
}

#endif
