// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/provider.hpp"

#include <cstdlib>
#include <stdexcept>

#include "gscope/nvidia_smi_provider.hpp"
#include "gscope/nvml_provider.hpp"
#include "gscope/proc_provider.hpp"
#include "gscope/tegrastats_provider.hpp"

namespace gscope {

bool commandExists(const std::string& command)
{
    std::string probe = "command -v " + command + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

std::unique_ptr<MetricsProvider> createProvider(const std::string& providerName,
                                                unsigned intervalMs)
{
    if (providerName == "tegrastats")
        return std::make_unique<TegrastatsProvider>(intervalMs);
    if (providerName == "nvml")
        return std::make_unique<NvmlProvider>(intervalMs);
    if (providerName == "nvidia-smi")
        return std::make_unique<NvidiaSmiProvider>(intervalMs);
    if (providerName == "proc")
        return std::make_unique<ProcFallbackProvider>(intervalMs);
    throw std::invalid_argument("unknown provider: " + providerName);
}

std::unique_ptr<MetricsProvider> detectProvider(unsigned intervalMs)
{
    if (const char* forced = std::getenv("GSCOPE_PROVIDER"))
        return createProvider(forced, intervalMs);

    if (commandExists("tegrastats"))
        return createProvider("tegrastats", intervalMs);
    // NVML (a direct library call per metric) beats shelling out to
    // `nvidia-smi` and parsing its CSV text; only fall back to the CLI
    // parser if the driver's NVML library isn't loadable for some reason
    // but the nvidia-smi binary still is.
    if (NvmlProvider::isAvailable())
        return createProvider("nvml", intervalMs);
    if (commandExists("nvidia-smi"))
        return createProvider("nvidia-smi", intervalMs);
    return createProvider("proc", intervalMs);
}

}  // namespace gscope
