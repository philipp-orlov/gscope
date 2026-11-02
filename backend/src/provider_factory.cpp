// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/provider.hpp"

#include <cstdlib>
#include <stdexcept>

#include "gscope/nvidia_smi_provider.hpp"
#include <unistd.h>

#include <cstdlib>
#include <string_view>

#include "gscope/nvml_provider.hpp"
#include "gscope/proc_provider.hpp"
#include "gscope/tegrastats_provider.hpp"

namespace gscope {

bool commandExists(const std::string& command)
{
    // A PATH scan with access(X_OK): no shell, so a name can never be interpreted as shell syntax.
    if (command.empty() || command.find('/') != std::string::npos)
        return !command.empty() && ::access(command.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    std::string_view rest = path ? path : "/usr/bin:/bin";
    while (!rest.empty()) {
        const size_t colon = rest.find(':');
        const std::string_view dir = rest.substr(0, colon);
        rest = colon == std::string_view::npos ? std::string_view{} : rest.substr(colon + 1);
        const std::string candidate = (dir.empty() ? std::string(".") : std::string(dir)) + "/" + command;
        if (::access(candidate.c_str(), X_OK) == 0)
            return true;
    }
    return false;
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
