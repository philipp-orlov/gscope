// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/tegrastats_provider.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <vector>

namespace {
void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}
}  // namespace

// Exercises the real background-thread read loop (not just the pure
// parser) with a VectorLineStream standing in for the tegrastats process --
// no real binary needed.
int main()
{
    std::vector<std::string> lines = {
        "09-22-2026 00:29:21 RAM 1000/2000MB (lfb 1x4MB) SWAP 0/100MB (cached 0MB) "
        "CPU [10%@1000] EMC_FREQ 0% GR3D_FREQ 5%@[0] GPU@40C",
        "09-22-2026 00:29:22 RAM 1100/2000MB (lfb 1x4MB) SWAP 0/100MB (cached 0MB) "
        "CPU [20%@1000] EMC_FREQ 0% GR3D_FREQ 15%@[0] GPU@41C",
    };

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<gscope::Sample> received;

    gscope::TegrastatsProvider provider(100, [lines] {
        return std::make_unique<gscope::VectorLineStream>(lines);
    });

    check(provider.start([&](const gscope::Sample& sample) {
              std::lock_guard<std::mutex> lock(mutex);
              received.push_back(sample);
              cv.notify_all();
          }),
          "provider starts with an injected line stream");

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::seconds(2), [&] { return received.size() >= 2; });
    }
    provider.stop();

    check(received.size() == 2, "both canned lines produced a sample");
    check(received[0].memUsedMb == 1000, "first sample parsed");
    check(received[1].memUsedMb == 1100, "second sample parsed");
    check(received[1].gpus[0].utilizationPercent == 15, "second sample's GPU utilization parsed");

    std::puts("tegrastats provider: injected line stream drives the real read loop");
    return 0;
}
