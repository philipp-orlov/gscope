// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: NVML-based provider for a discrete/dGPU NVIDIA machine that has
// no Jetson specifics -- the preferred path over shelling out to
// `nvidia-smi` text (see NvidiaSmiProvider) whenever the driver's NVML
// library is loadable: one direct library call per metric instead of a
// subprocess + CSV parse per sample.
//
// libnvidia-ml.so.1 ships with every NVIDIA Linux driver package (the same
// one `nvidia-smi` itself links against), so this needs no nvml.h/CUDA
// Toolkit dependency: the small, ABI-stable subset of the C API used here
// is redeclared below and resolved at runtime via dlopen/dlsym. That keeps
// this file -- and the Jetson build, which never has the driver -- compiling
// unconditionally everywhere; dlopen simply fails to find the library there
// and detectProvider() falls through to tegrastats/proc.
#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "gscope/proc_provider.hpp"
#include "gscope/provider.hpp"

namespace gscope {

// Matches NVML's `nvmlDevice_t` (a `struct nvmlDevice_st*`) bit-for-bit
// without needing nvml.h's declaration of the (never-defined) struct tag.
using NvmlDevice = void*;

struct NvmlUtilization
{
    unsigned int gpu = 0;
    unsigned int memory = 0;
};

struct NvmlMemory
{
    unsigned long long total = 0;
    unsigned long long free = 0;
    unsigned long long used = 0;
};

using NvmlInitFn = int (*)();
using NvmlShutdownFn = int (*)();
using NvmlDeviceGetCountFn = int (*)(unsigned int*);
using NvmlDeviceGetHandleByIndexFn = int (*)(unsigned int, NvmlDevice*);
using NvmlDeviceGetNameFn = int (*)(NvmlDevice, char*, unsigned int);
using NvmlDeviceGetUtilizationRatesFn = int (*)(NvmlDevice, NvmlUtilization*);
using NvmlDeviceGetMemoryInfoFn = int (*)(NvmlDevice, NvmlMemory*);
using NvmlDeviceGetTemperatureFn = int (*)(NvmlDevice, unsigned int, unsigned int*);
using NvmlDeviceGetPowerUsageFn = int (*)(NvmlDevice, unsigned int*);
using NvmlDeviceGetClockInfoFn = int (*)(NvmlDevice, unsigned int, unsigned int*);

// The subset of libnvidia-ml's entry points NvmlProvider needs, plus the
// dlopen() handle they were resolved from. Default-constructed (all null)
// means "not loaded"; explicit operator bool() reflects that. Tests inject
// a table of fake functions instead of the dlopen'd ones (dlHandle left
// null) to exercise NvmlProvider without the real driver.
struct NvmlApi
{
    void* dlHandle = nullptr;
    NvmlInitFn init = nullptr;
    NvmlShutdownFn shutdown = nullptr;
    NvmlDeviceGetCountFn deviceGetCount = nullptr;
    NvmlDeviceGetHandleByIndexFn deviceGetHandleByIndex = nullptr;
    NvmlDeviceGetNameFn deviceGetName = nullptr;
    NvmlDeviceGetUtilizationRatesFn deviceGetUtilizationRates = nullptr;
    NvmlDeviceGetMemoryInfoFn deviceGetMemoryInfo = nullptr;
    NvmlDeviceGetTemperatureFn deviceGetTemperature = nullptr;
    NvmlDeviceGetPowerUsageFn deviceGetPowerUsage = nullptr;
    NvmlDeviceGetClockInfoFn deviceGetClockInfo = nullptr;

    explicit operator bool() const
    {
        return init && shutdown && deviceGetCount && deviceGetHandleByIndex && deviceGetName &&
               deviceGetUtilizationRates && deviceGetMemoryInfo && deviceGetTemperature &&
               deviceGetPowerUsage && deviceGetClockInfo;
    }
};

// dlopen()s libnvidia-ml.so.1 and dlsym()s the entry points above. Returns a
// null NvmlApi (operator bool() false, dlHandle closed already) if the
// library or any expected symbol is missing -- not present at all (e.g. on
// Jetson) is the expected, non-error outcome on most machines.
NvmlApi loadNvmlApi();

// dlclose()s api.dlHandle (if any) and resets api to null. No-op on an
// already-null/injected (dlHandle == nullptr) api.
void unloadNvmlApi(NvmlApi& api);

// Fills one GpuSample from a device handle via api. Returns false only if
// even the name query fails (device gone/unreadable). memory.used/total
// unavailable (e.g. NVML_ERROR_NOT_SUPPORTED on a unified-memory NVIDIA SoC
// such as GB10/Grace-Blackwell) parse to NaN, exactly like
// parseNvidiaSmiCsvLine's [N/A] handling -- NvmlProvider mirrors system RAM
// onto those fields the same way NvidiaSmiProvider does. Exposed for tests.
bool nvmlFillGpuSample(const NvmlApi& api, NvmlDevice device, GpuSample& out);

class NvmlProvider : public MetricsProvider
{
public:
    // api defaults to being loaded lazily (loadNvmlApi()) in start(); tests
    // inject a fake table instead so no real driver is needed.
    explicit NvmlProvider(unsigned intervalMs = 1000, NvmlApi api = {});
    ~NvmlProvider() override;

    const char* name() const override { return "nvml"; }
    bool start(SampleCallback callback) override;
    void stop() override;

    // Best-effort probe used by detectProvider(): loads the library, calls
    // nvmlInit_v2()/nvmlShutdown(), and reports whether that succeeded --
    // without constructing/starting a full provider instance.
    static bool isAvailable();

private:
    void run(SampleCallback callback);

    unsigned intervalMs_;
    NvmlApi api_;
    bool ownApi_ = false;  // true if api_ came from loadNvmlApi() (needs unloadNvmlApi() in stop())
    unsigned deviceCount_ = 0;
    ProcCpuMemSampler cpuMemSampler_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

}  // namespace gscope
