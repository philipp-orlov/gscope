// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: metrics data model shared by every provider, the history
// buffer, JSON encoding and the tests.
#pragma once

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace gscope {

struct CpuCoreSample
{
    double utilizationPercent = 0.0;  // 0..100
    double frequencyMhz = 0.0;        // 0 if unknown
};

struct GpuSample
{
    std::string name;
    double utilizationPercent = 0.0;
    double memoryUsedMb = 0.0;
    double memoryTotalMb = 0.0;
    double temperatureC = -1000.0;  // sentinel: sensor not available
    double powerMw = -1.0;          // sentinel: not available
    double frequencyMhz = 0.0;
};

struct ProcessSample
{
    int pid = 0;
    std::string name;
    double cpuPercent = 0.0;  // relative to one core; a multi-threaded process can exceed 100
    double memoryMb = 0.0;    // resident set size
};

struct NetworkSample
{
    double rxBytesPerSec = 0.0;
    double txBytesPerSec = 0.0;
};

// One point-in-time reading. A provider produces these; nothing downstream
// (history buffer, JSON encoding, WebSocket broadcast) needs to know which
// provider built it.
struct Sample
{
    std::chrono::system_clock::time_point timestamp;
    std::vector<CpuCoreSample> cpuCores;
    double memUsedMb = 0.0;
    double memTotalMb = 0.0;
    double swapUsedMb = 0.0;
    double swapTotalMb = 0.0;
    std::vector<GpuSample> gpus;
    std::vector<ProcessSample> processes;  // top-N by CPU%, filled in by MetricsService
    NetworkSample network;                 // filled in by MetricsService, provider-independent
    std::vector<std::pair<std::string, double>> temperatures;  // named sensor, degC
};

}  // namespace gscope
