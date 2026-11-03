// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// A provider that emits only the samples a test hands it, on the test's
// own thread: no subprocess, no timing.
#pragma once

#include <cstdio>
#include <cstdlib>

#include "gscope/host_info.hpp"
#include "gscope/metrics.hpp"
#include "gscope/provider.hpp"

namespace gscope_test {

inline void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

class FakeProvider : public gscope::MetricsProvider
{
public:
    const char* name() const override { return "fake"; }
    bool start(SampleCallback callback) override
    {
        callback_ = std::move(callback);
        return true;
    }
    void stop() override {}
    void emit(const gscope::Sample& sample) { callback_(sample); }

private:
    SampleCallback callback_;
};

inline gscope::Sample makeSample(double cpuUtilization, double cpuTemperature = 50.0)
{
    gscope::Sample sample;
    sample.timestamp = std::chrono::system_clock::now();
    sample.cpuCores.push_back({cpuUtilization, 1500.0});
    sample.memUsedMb = 1000;
    sample.memTotalMb = 8000;
    sample.gpus.push_back({"NVIDIA GB10", 12.345, 4096, 122880, 41.26, 5600, 1800});
    sample.temperatures.push_back({"CPU", cpuTemperature});
    return sample;
}

inline gscope::HostInfo testHost()
{
    return {"test-host", "Linux 6.0", 20};
}

}  // namespace gscope_test
