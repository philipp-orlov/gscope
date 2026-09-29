// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: /proc-based CPU + memory sampling. Used standalone as the
// last-resort fallback provider (no GPU data, works on any Linux box) and
// reused by the nvidia-smi provider to fill in the CPU/RAM fields
// nvidia-smi itself doesn't report.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "gscope/metrics.hpp"
#include "gscope/provider.hpp"

namespace gscope {

struct ProcCpuTimes
{
    // One entry per core (as listed in /proc/stat's "cpuN" lines), each
    // the raw jiffie counters needed to compute a utilization delta
    // against the next reading.
    struct Core
    {
        uint64_t idle = 0;
        uint64_t total = 0;
    };
    std::vector<Core> cores;
};

// Parses /proc/stat's content (the "cpuN ..." lines only). Exposed for
// tests; production callers go through ProcCpuMemSampler::sample().
ProcCpuTimes parseProcStat(const std::string& content);

// Parses /proc/meminfo's content into the four fields Sample needs.
// Exposed for tests; production callers go through
// ProcCpuMemSampler::sample().
void parseProcMeminfo(const std::string& content, Sample& out);

// Two /proc/stat readings apart, per-core utilization percent is a delta
// of deltas: (busyB - busyA) / (totalB - totalA) * 100. Exposed for tests.
std::vector<double> cpuUtilizationPercent(const ProcCpuTimes& previous,
                                          const ProcCpuTimes& current);

// Stateful sampler: keeps the previous /proc/stat reading so consecutive
// sample() calls can report per-core utilization. The first call after
// construction always reports 0% for every core (no prior reading to
// diff against yet).
class ProcCpuMemSampler
{
public:
    // Reads real /proc/stat and /proc/meminfo.
    Sample sample();

private:
    bool havePrevious_ = false;
    ProcCpuTimes previous_;
};

// Fallback provider: CPU + memory only, no GPU. Polls ProcCpuMemSampler on
// a timer thread every intervalMs.
class ProcFallbackProvider : public MetricsProvider
{
public:
    explicit ProcFallbackProvider(unsigned intervalMs = 1000) : intervalMs_(intervalMs) {}
    ~ProcFallbackProvider() override;

    const char* name() const override { return "proc"; }
    bool start(SampleCallback callback) override;
    void stop() override;

private:
    unsigned intervalMs_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

}  // namespace gscope
