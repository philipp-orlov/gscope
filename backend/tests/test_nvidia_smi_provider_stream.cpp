// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/nvidia_smi_provider.hpp"

#include <chrono>
#include <cmath>
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

// Exercises the real background-thread read loop (not just the pure parser)
// with a VectorLineStream standing in for nvidia-smi -- no real binary
// needed. Covers a unified-memory GPU (e.g. GB10/Grace-Blackwell) whose
// memory.used/memory.total come back [N/A]: NvidiaSmiProvider must mirror
// system RAM onto those fields rather than leaving them at 0.
int main()
{
    std::vector<std::string> lines = {
        "0, NVIDIA GB10, 0, [N/A], [N/A], 36, 5.38, 208",
        "0, NVIDIA GB10, 5, [N/A], [N/A], 37, 6.10, 210",
    };

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<gscope::Sample> received;

    gscope::NvidiaSmiProvider provider(100, 1, [lines] {
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
    check(received[0].gpus.size() == 1, "one GPU parsed per sample");
    check(!std::isnan(received[0].gpus[0].memoryUsedMb), "N/A memory.used got mirrored, not left NaN");
    check(!std::isnan(received[0].gpus[0].memoryTotalMb), "N/A memory.total got mirrored, not left NaN");
    check(received[0].gpus[0].memoryUsedMb == received[0].memUsedMb,
          "mirrored GPU memory.used matches system RAM used");
    check(received[0].gpus[0].memoryTotalMb == received[0].memTotalMb,
          "mirrored GPU memory.total matches system RAM total");
    check(received[1].gpus[0].utilizationPercent == 5, "second sample's GPU utilization parsed");

    std::puts("nvidia-smi provider: unified-memory GPU mirrors system RAM instead of NaN/0");
    return 0;
}
