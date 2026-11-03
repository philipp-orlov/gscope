// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/host_info.hpp"

#include <sys/utsname.h>
#include <unistd.h>

namespace gscope {

HostInfo HostInfo::local()
{
    HostInfo info;
    char buffer[256] = {0};
    info.hostname = gethostname(buffer, sizeof(buffer) - 1) == 0 ? buffer : "unknown";
    struct utsname name;
    info.kernel = uname(&name) == 0 ? std::string(name.sysname) + " " + name.release : "unknown";
    const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    info.cpuCount = cpus > 0 ? static_cast<uint32_t>(cpus) : 0;
    return info;
}

}  // namespace gscope
