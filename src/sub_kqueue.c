// Copyright (c) 2017-2018 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#include "platform.h"

#if (MBEAT_EVENT == MBEAT_EVENT_KQUEUE)

#include <sys/event.h>

#include <string.h>

#include "types.h"
#include "common.h"
#include "sub.h"


static int epfd; ///< Event queue.

/// Create a new event queue.
/// @return status code
bool
create_event_queue(void)
{
  notify(NL_DEBUG, false, "Using the %s event queue", "kqueue");

  eqfd = kqueue();
  if (eqfd < 0) {
    notify(NL_ERROR, true, "Unable to create event queue");
    return false;
  }

  return true;
}

/// Add a socket to the event queue.
/// @return status code
///
/// @param[in] ep endpoint
bool
add_socket_event(endpoint* ep)
{
  struct kevent ev;

  // Create a new event, where the auxiliary payload points to the endpoint,
  // where more information can be accessed by the handler function.
  EV_SET(&ev, ep->ep_sock, EVFILT_READ, EV_ADD, 0, 0, ep);
  if (kevent(eqfd, &ev, 1, NULL, 0, NULL) == -1) {
    notify(NL_ERROR, true, "Unable to add a socket to the event queue");
    return false;
  }

  return true;
}

bool
add_signal_events(void)
{
  struct kevent ev;

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
}

/// Notify the user the type of the received signal.
static bool
report_signal(struct kevent* ev)
{
  notify(NL_INFO, false, "Received the %s signal", strsignal(ev.data));
  return true;
}

/// Process the incoming network datagrams and process signals.
/// @return status code
///
/// @param[in] eps endpoints list
bool
receive_events(endpoint* eps)
{
  struct kevent evs[64];
  int cnt;
  int i;

  // The endpoints list is not used - this line serves to suppress the compiler
  // warning for an unused function argument.
  (void)eps;

  cnt = kevent(eqfd, NULL, 0, evs, 64, NULL);
  if (cnt < 0) {
    notify(NL_ERROR, true, "Unable to retrieve events");
    return false;
  }

  for (i = 0; i < cnt; i++) {
    notify(NL_TRACE, false, "Received event %d/%d", i + 1, cnt);

    // Handle the signal event for SIGINT and SIGHUP.
    if (evs[i].filter == EVFILT_SIGNAL)
      return report_signal(&ev[i]);

    // Handle socket events.
    if (!handle_event(evs[i].udata))
      return false;
  }

  return true;
}

#endif
