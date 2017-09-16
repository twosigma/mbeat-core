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

// Maximal number of allowed endpoints.
#define ENDPOINT_MAX 2048

bool allocate_endpoints(endpoint** eps, const int ep_cnt);
bool cache_hostname(char* hname, const size_t hname_len);
void convert_nanos(struct timespec* tv, const uint64_t ms);
uint64_t htonll(const uint64_t x);
uint64_t ntohll(const uint64_t x);

#endif
