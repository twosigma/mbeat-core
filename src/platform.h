// Copyright (c) 2017-2018 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#ifndef MBEAT_PLATFORM_H
#define MBEAT_PLATFORM_H

// This header defines three macros: MBEAT_EVENT_PSELECT, MBEAT_EVENT_KQUEUE,
// and MBEAT_EVENT_EPOLL. Based on the operating system and available event
// queue, one of these values becomes the value of MBEAT_EVENT, which is
// used to include the correct code.
#define MBEAT_EVENT_PSELECT 0
#define MBEAT_EVENT_EPOLL   1
#define MBEAT_EVENT_KQUEUE  2

// Enforce the fully POSIX-compliant behaviour. This way it is possible to use
// the standard event queue even on systems that support more advanced queues.
#if defined(MBEAT_FORCE_POSIX)
  #define MBEAT_EVENT MBEAT_EVENT_PSELECT
#else
  #if defined(__linux__)
    #define MBEAT_EVENT MBEAT_EVENT_EPOLL
  #elif defined(__FreeBSD__) \
     || defined(__NetBSD__)  \
     || defined(__OpenBSD__) \
     || defined(__DragonFly__)
    #define MBEAT_EVENT MBEAT_EVENT_KQUEUE
  #else
    #define MBEAT_EVENT MBEAT_EVENT_PSELECT
  #endif
#endif

#endif
