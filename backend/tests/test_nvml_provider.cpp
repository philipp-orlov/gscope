// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/nvml_provider.hpp"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

// A fake single-GPU NVML: unified memory (memory query "not supported",
// like GB10/Grace-Blackwell), everything else a plausible reading. Mirrors
// the shape of a real driver closely enough to exercise NvmlProvider's
// dispatch/mirroring logic without needing the real library.
constexpr unsigned kFakeDeviceCount = 1;

int fakeInit() { return 0; }
int fakeShutdown() { return 0; }

int fakeDeviceGetCount(unsigned int* count)
{
    *count = kFakeDeviceCount;
    return 0;
}

int fakeDeviceGetHandleByIndex(unsigned int index, gscope::NvmlDevice* device)
{
    *device = reinterpret_cast<gscope::NvmlDevice>(static_cast<std::uintptr_t>(index + 1));
    return 0;
}

int fakeDeviceGetName(gscope::NvmlDevice, char* name, unsigned int length)
{
    std::snprintf(name, length, "NVIDIA GB10");
    return 0;
}

int fakeDeviceGetUtilizationRates(gscope::NvmlDevice, gscope::NvmlUtilization* util)
{
    util->gpu = 5;
    util->memory = 0;
    return 0;
}

int fakeDeviceGetMemoryInfo(gscope::NvmlDevice, gscope::NvmlMemory*)
{
    return 3;  // NVML_ERROR_NOT_SUPPORTED: unified memory, no separate GPU pool
}

int fakeDeviceGetTemperature(gscope::NvmlDevice, unsigned int, unsigned int* temp)
{
    *temp = 37;
    return 0;
}

int fakeDeviceGetPowerUsage(gscope::NvmlDevice, unsigned int* power)
{
    *power = 6100;  // milliwatts, already -- unlike the nvidia-smi CLI's watts
    return 0;
}

int fakeDeviceGetClockInfo(gscope::NvmlDevice, unsigned int, unsigned int* clock)
{
    *clock = 210;
    return 0;
}

gscope::NvmlApi makeFakeApi()
{
    gscope::NvmlApi api;
    api.init = fakeInit;
    api.shutdown = fakeShutdown;
    api.deviceGetCount = fakeDeviceGetCount;
    api.deviceGetHandleByIndex = fakeDeviceGetHandleByIndex;
    api.deviceGetName = fakeDeviceGetName;
    api.deviceGetUtilizationRates = fakeDeviceGetUtilizationRates;
    api.deviceGetMemoryInfo = fakeDeviceGetMemoryInfo;
    api.deviceGetTemperature = fakeDeviceGetTemperature;
    api.deviceGetPowerUsage = fakeDeviceGetPowerUsage;
    api.deviceGetClockInfo = fakeDeviceGetClockInfo;
    return api;
}
}  // namespace

int main()
{
    gscope::NvmlApi api = makeFakeApi();
    check(static_cast<bool>(api), "fake NvmlApi has every entry point populated");

    // Unit-level: nvmlFillGpuSample() directly.
    gscope::GpuSample gpu;
    check(gscope::nvmlFillGpuSample(api, reinterpret_cast<gscope::NvmlDevice>(1), gpu),
          "nvmlFillGpuSample succeeds when the name query succeeds");
    check(gpu.name == "NVIDIA GB10", "name");
    check(gpu.utilizationPercent == 5, "utilization");
    check(std::isnan(gpu.memoryUsedMb), "NOT_SUPPORTED memory.used parses to NaN");
    check(std::isnan(gpu.memoryTotalMb), "NOT_SUPPORTED memory.total parses to NaN");
    check(gpu.temperatureC == 37, "temperature");
    check(gpu.powerMw == 6100, "power already in milliwatts, no unit conversion");
    check(gpu.frequencyMhz == 210, "clock");

    // Provider-level: the real background thread + read loop, driven by the
    // injected fake API instead of the real driver. Covers a unified-memory
    // GPU whose memory query fails: NvmlProvider must mirror system RAM
    // rather than leaving memoryUsedMb/memoryTotalMb at NaN.
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<gscope::Sample> received;

    gscope::NvmlProvider provider(20, api);

    check(provider.start([&](const gscope::Sample& sample) {
              std::lock_guard<std::mutex> lock(mutex);
              received.push_back(sample);
              cv.notify_all();
          }),
          "provider starts with an injected fake NVML API");

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::seconds(2), [&] { return received.size() >= 2; });
    }
    provider.stop();

    check(received.size() >= 2, "at least two samples were produced");
    check(received[0].gpus.size() == 1, "one GPU parsed per sample");
    check(!std::isnan(received[0].gpus[0].memoryUsedMb), "NaN memory.used got mirrored, not left NaN");
    check(!std::isnan(received[0].gpus[0].memoryTotalMb), "NaN memory.total got mirrored, not left NaN");
    check(received[0].gpus[0].memoryUsedMb == received[0].memUsedMb,
          "mirrored GPU memory.used matches system RAM used");
    check(received[0].gpus[0].memoryTotalMb == received[0].memTotalMb,
          "mirrored GPU memory.total matches system RAM total");

    std::puts("nvml provider: fake API dispatch, NaN fallback, and unified-memory mirroring passed");
    return 0;
}
