// Copyright (c) 2017 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#ifndef MBEAT_COMMON_H
#define MBEAT_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "types.h"

/// Global logging message level threshold.
extern uint8_t glvl;

/// Hostname.
extern char hname[HNAME_LEN];

// Semantic versioning scheme.
#define MBEAT_VERSION_MAJOR 1
#define MBEAT_VERSION_MINOR 8
#define MBEAT_VERSION_PATCH 0

// Standard UDP port.
#define MBEAT_PORT 22999

// Payload-related constants.
#define MBEAT_PAYLOAD_MAGIC   0x6d626974
#define MBEAT_PAYLOAD_VERSION          2

// Notification levels.
#define NL_ERROR 0 // Error.
#define NL_WARN  1 // Warning.
#define NL_INFO  2 // Information.
#define NL_DEBUG 3 // Debug.
#define NL_TRACE 4 // Tracing.

// Maximal number of allowed endpoints. It is not clear yet what this number
// should be, but given the availability of specifying IP-address ranges, this
// number must cover a small number of /8 subnets. The current constant is
// equal to (2^24) * 5.
#define ENDPOINT_MAX 83886080

void free_endpoints(endpoint* eps);
bool cache_hostname(void);
void convert_nanos(struct timespec* tv, const uint64_t ms);
uint64_t htonll(const uint64_t x);
uint64_t ntohll(const uint64_t x);
void notify(const uint8_t lvl, const bool perr, const char* msg, ...);

#endif
