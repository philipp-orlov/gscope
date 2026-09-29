// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: network in/out throughput, read from /proc/net/dev -- like
// ProcessSampler, available regardless of which MetricsProvider is active.
#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

#include "gscope/metrics.hpp"

namespace gscope {

struct ProcNetDevCounters
{
    uint64_t rxBytes = 0;
    uint64_t txBytes = 0;
};

// Parses /proc/net/dev's content (the two header lines are skipped) into
// per-interface byte counters. Exposed for tests.
std::unordered_map<std::string, ProcNetDevCounters> parseProcNetDev(const std::string& content);

// Sums every interface except "lo" (loopback) -- the policy for "what
// counts as network traffic" here. Exposed for tests.
ProcNetDevCounters sumNonLoopback(const std::unordered_map<std::string, ProcNetDevCounters>& byInterface);

class NetworkSampler
{
public:
    // Reads the real /proc/net/dev; keeps the previous reading so
    // consecutive calls can report a rate. The first call after
    // construction always reports 0 B/s (no prior reading to diff
    // against yet).
    NetworkSample sample();

private:
    bool havePrevious_ = false;
    ProcNetDevCounters previous_;
    std::chrono::steady_clock::time_point previousTime_;
};

}  // namespace gscope
