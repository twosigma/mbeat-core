/*
 *  Copyright (c) 2017 Two Sigma Open Source, LLC.
 *  All Rights Reserved
 *
 *  Distributed under the terms of the 2-clause BSD License. The full
 *  license is in the file LICENSE, distributed as part of this software.
**/

#ifndef MBEAT_COMMON_H
#define MBEAT_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "types.h"


/* Semantic versioning scheme. */
#define MBEAT_VERSION_MAJOR 1
#define MBEAT_VERSION_MINOR 0
#define MBEAT_VERSION_PATCH 0

/* Standard UDP port. */
#define MBEAT_PORT 22999

#define PAYLOAD_VERSION    1 /* Accepted payload version.            */
#define ENDPOINT_MAX    2048 /* Maximal number of allowed endpoints. */

bool parse_uint32(uint32_t* out,
                  const char* str,
                  const uint32_t min,
                  const uint32_t max);
bool parse_endpoints(endpoint* eps,
                     const int ep_idx,
                     char* argv[],
                     const int ep_cnt);
bool allocate_endpoints(endpoint** eps, const int ep_cnt);
bool cache_hostname(char* hname, const size_t hname_len);
void convert_millis(struct timespec* tv, const uint32_t ms);

#endif
