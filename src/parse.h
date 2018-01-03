// Copyright (c) 2017-2018 Two Sigma Open Source, LLC.
// All Rights Reserved
//
// Distributed under the terms of the 2-clause BSD License. The full
// license is in the file LICENSE, distributed as part of this software.

#ifndef MBEAT_PARSE_H
#define MBEAT_PARSE_H

#include <stdbool.h>
#include <stdint.h>

#include "types.h"


bool parse_uint64(uint64_t* out,
                  const char* str,
                  const uint64_t min,
                  const uint64_t max);

bool parse_duration(uint64_t* out, const char* inp);
bool parse_endpoints(endpoint** eps,
                     const int ep_idx,
                     char* argv[],
                     const int ep_cnt);

#endif
