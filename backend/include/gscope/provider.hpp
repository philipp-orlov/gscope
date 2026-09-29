// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: the provider hook. This is the seam that lets the backend run
// on a Jetson (tegrastats), a machine with a discrete NVIDIA GPU (nvml, or
// nvidia-smi as a fallback) or anything else with a /proc filesystem
// (cpu/mem only, same shape nvtop/btop fall back to when no GPU is found) --
// callers past detectProvider()/createProvider() never see which one is in
// use.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "gscope/metrics.hpp"

namespace gscope {

class MetricsProvider
{
public:
    using SampleCallback = std::function<void(const Sample&)>;

    virtual ~MetricsProvider() = default;

    // Short identifier surfaced via /api/system, e.g. "tegrastats".
    virtual const char* name() const = 0;

    // Starts a background sampling thread that invokes callback once per
    // reading. Returns false if the underlying tool could not be started
    // (missing binary, spawn failure). Safe to call once; call stop()
    // before destruction.
    virtual bool start(SampleCallback callback) = 0;

    // Stops the background thread and joins it. Idempotent.
    virtual void stop() = 0;
};

// Picks a provider for the current machine by probing for the tools it
// needs, in order: tegrastats (Jetson), nvml (discrete NVIDIA GPU with a
// loadable driver), nvidia-smi (same, if NVML didn't load), then a
// /proc-only fallback that always succeeds. The GSCOPE_PROVIDER environment
// variable ("tegrastats" | "nvml" | "nvidia-smi" | "proc") overrides
// detection, e.g. to force the fallback on a Jetson for testing.
std::unique_ptr<MetricsProvider> detectProvider(unsigned intervalMs = 1000);

// Builds one provider by name, throwing std::invalid_argument for an
// unknown name. Used by detectProvider() and directly by callers/tests
// that want a specific provider rather than autodetection.
std::unique_ptr<MetricsProvider> createProvider(const std::string& providerName,
                                                unsigned intervalMs);

// True if `command` resolves to an executable via $PATH (a plain,
// dependency-free stand-in for `which`).
bool commandExists(const std::string& command);

}  // namespace gscope
