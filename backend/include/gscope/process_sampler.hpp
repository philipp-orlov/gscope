// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: top-N running processes by CPU%, read straight from /proc --
// works on any Linux box regardless of which MetricsProvider is in use,
// the same way top/htop compute it.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "gscope/metrics.hpp"

namespace gscope {

struct ProcPidStat
{
    int pid = 0;
    std::string name;
    uint64_t utimeTicks = 0;
    uint64_t stimeTicks = 0;
};

// Parses a /proc/[pid]/stat line. The process name sits in parens and may
// itself contain spaces or parens, so the name is read between the first
// '(' and the last ')' rather than by splitting on spaces throughout.
// Exposed for tests. Returns false if the line is too short to contain
// utime/stime.
bool parseProcPidStat(std::string_view content, ProcPidStat& out);

// Parses "VmRSS:    1234 kB" out of /proc/[pid]/status. Exposed for tests.
// Returns 0 if the line isn't present (e.g. the process already exited).
double parseVmRssKb(std::string_view statusContent);

// (currentTicks - previousTicks) turned into a %CPU relative to one core
// over `elapsedSeconds` -- a multi-threaded process can exceed 100%, same
// as top/ps. Exposed for tests. Returns 0 for a first-seen pid (no
// previous reading), a non-positive elapsed time, or a pid whose tick
// counter went backwards (it was reused by a new process).
double cpuPercentFromTicks(uint64_t previousTicks, uint64_t currentTicks, double elapsedSeconds,
                           long ticksPerSecond);

class ProcessSampler
{
public:
    // Real /proc read; keeps the previous reading's ticks per pid so
    // consecutive calls can report %CPU. The first call after
    // construction always reports 0% for every process.
    std::vector<ProcessSample> sample(size_t topN = 10);

private:
    std::unordered_map<int, uint64_t> previousTicks_;
    std::chrono::steady_clock::time_point previousTime_;
    bool havePrevious_ = false;
};

}  // namespace gscope
