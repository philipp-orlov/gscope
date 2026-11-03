// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: who the daemon runs on, for the dashboard's header (part of the
// wire Inventory).
#pragma once

#include <cstdint>
#include <string>

namespace gscope {

struct HostInfo
{
    std::string hostname;
    std::string kernel;    // "Linux 6.8.0-..."
    uint32_t cpuCount = 0;

    // This machine: gethostname(), uname(), the online CPUs.
    static HostInfo local();

    bool operator==(const HostInfo& other) const
    {
        return hostname == other.hostname && kernel == other.kernel && cpuCount == other.cpuCount;
    }
    bool operator!=(const HostInfo& other) const { return !(*this == other); }
};

}  // namespace gscope
