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


// Semantic versioning scheme.
#define MBEAT_VERSION_MAJOR 1
#define MBEAT_VERSION_MINOR 6
#define MBEAT_VERSION_PATCH 0

// Standard UDP port.
#define MBEAT_PORT 22999

// Payload-related constants.
#define MBEAT_PAYLOAD_MAGIC   0x6d626974
#define MBEAT_PAYLOAD_VERSION          2

// Maximal number of allowed endpoints. It is not clear yet what this number
// should be, but given the availability of specifying IP-address ranges, this
// number must cover a small number of /8 subnets. The current constant is
// equal to (2^24) * 5.
#define ENDPOINT_MAX 83886080

void free_endpoints(endpoint* eps);
bool cache_hostname(char* hname, const size_t hname_len);
void convert_nanos(struct timespec* tv, const uint64_t ms);
uint64_t htonll(const uint64_t x);
uint64_t ntohll(const uint64_t x);

#endif
