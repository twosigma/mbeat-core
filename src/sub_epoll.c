// Copyright (c) 2017-2018 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#include "platform.h"

// This check has to be preceded by the "platform.h" include, as it defines
// the MBEAT_EVENT_* macros based on the operating system.
#if (MBEAT_EVENT == MBEAT_EVENT_EPOLL)

#include <sys/epoll.h>
#include <sys/signalfd.h>

#include <unistd.h>
#include <string.h>

#include "sub.h"
#include "common.h"


static int eqfd;  ///< Event queue.
static int sigfd; ///< Signal event.

/// Create a new event queue.
/// @return status code
bool
create_event_queue(void)
{
  notify(NL_DEBUG, false, "Using the %s event queue", "epoll");

  eqfd = epoll_create(ENDPOINT_MAX);
  if (eqfd < 0) {
    notify(NL_ERROR, true, "Unable to create event queue");
    return false;
  }

  return true;
}

/// Register a socket with the event queue.
/// @return status code
///
/// @param[in] ep endpoint
bool
add_socket_event(endpoint* ep)
{
  struct epoll_event ev;

  ev.events = EPOLLIN;
  ev.data.ptr = ep;

  // Create a new event, where the auxiliary payload points to the endpoint,
  // where more information can be accessed by the handler function.
  notify(NL_TRACE, false, "Adding endpoint socket to the event queue");
  if (epoll_ctl(eqfd, EPOLL_CTL_ADD, ep->ep_sock, &ev) == -1) {
    notify(NL_ERROR, true, "Unable to add a socket to the event queue");
    return false;
  }

  return true;
}

/// Register events for signals SIGINT and SIGHUP.
/// @return status code
bool
add_signal_events(void)
{
  struct epoll_event ev;
  sigset_t mask;

  // Create the signal mask.
  if (create_signal_mask(&mask) == false) {
    notify(NL_ERROR, false, "Unable to create the signal mask");
    return false;
  }

  // Create a new signal file descriptor.
  notify(NL_TRACE, false, "Creating a signal file descriptor");
  sigfd = signalfd(-1, &mask, 0);
  if (sigfd == -1) {
    notify(NL_ERROR, true, "Unable to create a signal file descriptor");
    return false;
  }

  // Add the signal file descriptor to the event queue.
  notify(NL_TRACE, false, "Adding a signal to the event queue");
  ev.events = EPOLLIN;
  ev.data.fd = sigfd;
  if (epoll_ctl(eqfd, EPOLL_CTL_ADD, sigfd, &ev) == -1) {
    notify(NL_ERROR, true, "Unable to add a signal to the event queue");
    return false;
  }

  return true;
}

/// Notify the user the type of the received signal.
static bool
report_signal(void)
{
  struct signalfd_siginfo ssi;
  ssize_t nbytes;

  nbytes = read(sigfd, &ssi, sizeof(ssi));
  if (nbytes < 0) {
    notify(NL_ERROR, true, "Unable to retrieve the signal information");
    return false;
  }

  if ((size_t)nbytes != sizeof(ssi)) {
    notify(NL_ERROR, false, "Unable to retrieve full signal information");
    return false;
  }

  notify(NL_INFO, false, "Received the %s signal", strsignal(ssi.ssi_signo));
  return true;
}

/// Process the incoming network datagrams and process signals.
/// @return status code
///
/// @param[in] eps endpoints list
bool
receive_events(endpoint* eps)
{
  struct epoll_event evs[64];
  int cnt;
  int i;

  // The endpoints list is not used - this line serves to suppress the compiler
  // warning for an unused function argument.
  (void)eps;

  while (1) {
    notify(NL_DEBUG, false, "Waiting for events");

    // Read events from the event queue.
    cnt = epoll_wait(eqfd, evs, 64, -1);
    if (cnt < 0) {
      notify(NL_ERROR, true, "Event queue reading failed");
      return false;
    }

    for (i = 0; i < cnt; i++) {
      notify(NL_TRACE, false, "Received event %d/%d", i + 1, cnt);

      // Handle the signal event for SIGINT and SIGHUP.
      if (evs[i].data.fd == sigfd)
        return report_signal();

      // Handle socket events.
      if (!handle_event(evs[i].data.ptr))
        return false;
    }
  }

  return true;
}

#endif
