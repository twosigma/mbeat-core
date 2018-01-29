// Copyright (c) 2017-2018 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#ifndef MBEAT_SUB_H
#define MBEAT_SUB_H

#include <stdbool.h>
#include <signal.h>

#include "types.h"


// The following four functions have to be implemented by every event queue.
bool create_event_queue(void);
bool add_socket_event(endpoint* ep);
bool add_signal_events(void);
bool receive_events(endpoint* eps);

// The following functions are used by the event queues.
bool create_signal_mask(sigset_t* mask);
bool handle_event(endpoint* ep);

#endif
