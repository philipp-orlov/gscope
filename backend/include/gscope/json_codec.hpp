// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: the one place a Sample becomes wire JSON, so the REST
// endpoints, the WebSocket broadcast and the tests all agree on the
// schema. See frontend/README for the documented shape consumed by the
// Angular client.
#pragma once

#include <string>

#include "gscope/metrics.hpp"
#include "json/json.hpp"

namespace gscope {

json::Json sampleToJson(const Sample& sample);

}  // namespace gscope
