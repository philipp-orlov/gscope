// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/nvml_provider.hpp"

#include <cmath>
#include <chrono>
#include <cstring>

#include <dlfcn.h>

#include "http/logger.hpp"

namespace gscope {
namespace {

constexpr int kNvmlSuccess = 0;
constexpr unsigned int kNvmlTemperatureGpu = 0;  // NVML_TEMPERATURE_GPU
constexpr unsigned int kNvmlClockSm = 1;         // NVML_CLOCK_SM

template <typename Fn>
bool loadSymbol(void* handle, const char* symbolName, Fn& out)
{
    out = reinterpret_cast<Fn>(dlsym(handle, symbolName));
    return out != nullptr;
}

}  // namespace

NvmlApi loadNvmlApi()
{
    NvmlApi api;
    void* handle = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return api;  // no NVIDIA driver/NVML on this machine (e.g. Jetson) -- not an error

    bool ok = loadSymbol(handle, "nvmlInit_v2", api.init) &&
              loadSymbol(handle, "nvmlShutdown", api.shutdown) &&
              loadSymbol(handle, "nvmlDeviceGetCount_v2", api.deviceGetCount) &&
              loadSymbol(handle, "nvmlDeviceGetHandleByIndex_v2", api.deviceGetHandleByIndex) &&
              loadSymbol(handle, "nvmlDeviceGetName", api.deviceGetName) &&
              loadSymbol(handle, "nvmlDeviceGetUtilizationRates", api.deviceGetUtilizationRates) &&
              loadSymbol(handle, "nvmlDeviceGetMemoryInfo", api.deviceGetMemoryInfo) &&
              loadSymbol(handle, "nvmlDeviceGetTemperature", api.deviceGetTemperature) &&
              loadSymbol(handle, "nvmlDeviceGetPowerUsage", api.deviceGetPowerUsage) &&
              loadSymbol(handle, "nvmlDeviceGetClockInfo", api.deviceGetClockInfo);
    if (!ok) {
        HTTP_LOG_WARN("nvml: libnvidia-ml.so.1 loaded but missing an expected symbol");
        dlclose(handle);
        return NvmlApi{};
    }

    api.dlHandle = handle;
    return api;
}

void unloadNvmlApi(NvmlApi& api)
{
    if (api.dlHandle)
        dlclose(api.dlHandle);
    api = NvmlApi{};
}

bool nvmlFillGpuSample(const NvmlApi& api, NvmlDevice device, GpuSample& out)
{
    out = GpuSample{};
    // NaN (rather than 0) marks memory as unreported so the caller can tell
    // "unified memory, mirror system RAM" apart from a real 0 MB reading --
    // see the header comment.
    out.memoryUsedMb = std::nan("");
    out.memoryTotalMb = std::nan("");

    char name[96] = {0};
    if (api.deviceGetName(device, name, sizeof(name)) != kNvmlSuccess)
        return false;
    out.name.assign(name);

    NvmlUtilization util;
    if (api.deviceGetUtilizationRates(device, &util) == kNvmlSuccess)
        out.utilizationPercent = util.gpu;

    NvmlMemory mem;
    if (api.deviceGetMemoryInfo(device, &mem) == kNvmlSuccess) {
        out.memoryUsedMb = static_cast<double>(mem.used) / (1024.0 * 1024.0);
        out.memoryTotalMb = static_cast<double>(mem.total) / (1024.0 * 1024.0);
    }

    unsigned int tempC = 0;
    if (api.deviceGetTemperature(device, kNvmlTemperatureGpu, &tempC) == kNvmlSuccess)
        out.temperatureC = tempC;

    unsigned int powerMw = 0;
    if (api.deviceGetPowerUsage(device, &powerMw) == kNvmlSuccess)
        out.powerMw = powerMw;  // NVML already reports milliwatts

    unsigned int clockMhz = 0;
    if (api.deviceGetClockInfo(device, kNvmlClockSm, &clockMhz) == kNvmlSuccess)
        out.frequencyMhz = clockMhz;

    return true;
}

NvmlProvider::NvmlProvider(unsigned intervalMs, NvmlApi api)
    : intervalMs_(intervalMs), api_(api)
{}

NvmlProvider::~NvmlProvider()
{
    stop();
}

bool NvmlProvider::isAvailable()
{
    NvmlApi api = loadNvmlApi();
    if (!api)
        return false;
    bool ok = api.init() == kNvmlSuccess;
    if (ok)
        api.shutdown();
    unloadNvmlApi(api);
    return ok;
}

bool NvmlProvider::start(SampleCallback callback)
{
    if (running_.exchange(true))
        return true;

    if (!api_) {
        api_ = loadNvmlApi();
        ownApi_ = true;
    }
    if (!api_) {
        running_ = false;
        return false;
    }

    if (api_.init() != kNvmlSuccess) {
        running_ = false;
        return false;
    }

    unsigned int count = 0;
    if (api_.deviceGetCount(&count) != kNvmlSuccess || count == 0) {
        api_.shutdown();
        running_ = false;
        return false;
    }
    deviceCount_ = count;

    worker_ = std::thread(&NvmlProvider::run, this, std::move(callback));
    return true;
}

void NvmlProvider::stop()
{
    if (!running_.exchange(false))
        return;
    // No blocked I/O to interrupt (unlike PopenLineStream) -- run() just
    // sleeps between samples, so join() alone unblocks promptly.
    if (worker_.joinable())
        worker_.join();
    if (api_)
        api_.shutdown();
    if (ownApi_)
        unloadNvmlApi(api_);
}

void NvmlProvider::run(SampleCallback callback)
{
    while (running_.load()) {
        Sample sample = cpuMemSampler_.sample();
        for (unsigned i = 0; i < deviceCount_; ++i) {
            NvmlDevice device = nullptr;
            if (api_.deviceGetHandleByIndex(i, &device) != kNvmlSuccess) {
                HTTP_LOG_WARN("nvml: device handle unavailable, skipped");
                continue;
            }
            GpuSample gpu;
            if (!nvmlFillGpuSample(api_, device, gpu)) {
                HTTP_LOG_WARN("nvml: device query failed, skipped");
                continue;
            }
            // Unified memory (e.g. GB10/Grace-Blackwell): mirror system RAM
            // the same way NvidiaSmiProvider/TegrastatsProvider do.
            if (std::isnan(gpu.memoryUsedMb))
                gpu.memoryUsedMb = sample.memUsedMb;
            if (std::isnan(gpu.memoryTotalMb))
                gpu.memoryTotalMb = sample.memTotalMb;
            // Surface each GPU's temperature in the generic sensor table too
            // (thermal_zone sysfs, read above, is often empty on servers
            // that lack ACPI thermal zones).
            if (gpu.temperatureC > -1000.0)
                sample.temperatures.emplace_back("GPU " + std::to_string(i), gpu.temperatureC);
            sample.gpus.push_back(std::move(gpu));
        }
        callback(sample);
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_));
    }
}

}  // namespace gscope
