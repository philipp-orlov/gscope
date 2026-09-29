// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/network_sampler.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace gscope {
namespace {

std::string readFile(const char* path)
{
    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

}  // namespace

std::unordered_map<std::string, ProcNetDevCounters> parseProcNetDev(const std::string& content)
{
    std::unordered_map<std::string, ProcNetDevCounters> result;
    std::istringstream lines(content);
    std::string line;
    int lineNumber = 0;
    while (std::getline(lines, line)) {
        ++lineNumber;
        if (lineNumber <= 2)
            continue;  // "Inter-|   Receive ..." / " face |bytes packets ..."

        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;

        std::string name = line.substr(0, colon);
        size_t start = name.find_first_not_of(' ');
        if (start == std::string::npos)
            continue;
        name = name.substr(start);

        std::istringstream fields(line.substr(colon + 1));
        std::vector<uint64_t> values;
        uint64_t value = 0;
        while (fields >> value) values.push_back(value);
        // rxBytes(0) rxPackets rxErrs rxDrop rxFifo rxFrame rxCompressed
        // rxMulticast(7) txBytes(8) ...
        if (values.size() < 9)
            continue;

        ProcNetDevCounters counters;
        counters.rxBytes = values[0];
        counters.txBytes = values[8];
        result[name] = counters;
    }
    return result;
}

ProcNetDevCounters sumNonLoopback(const std::unordered_map<std::string, ProcNetDevCounters>& byInterface)
{
    ProcNetDevCounters total;
    for (auto& [name, counters] : byInterface) {
        if (name == "lo")
            continue;
        total.rxBytes += counters.rxBytes;
        total.txBytes += counters.txBytes;
    }
    return total;
}

NetworkSample NetworkSampler::sample()
{
    NetworkSample out;
    ProcNetDevCounters counters = sumNonLoopback(parseProcNetDev(readFile("/proc/net/dev")));
    const auto now = std::chrono::steady_clock::now();

    if (havePrevious_) {
        const double elapsedSeconds = std::chrono::duration<double>(now - previousTime_).count();
        if (elapsedSeconds > 0 && counters.rxBytes >= previous_.rxBytes &&
            counters.txBytes >= previous_.txBytes) {
            out.rxBytesPerSec = static_cast<double>(counters.rxBytes - previous_.rxBytes) / elapsedSeconds;
            out.txBytesPerSec = static_cast<double>(counters.txBytes - previous_.txBytes) / elapsedSeconds;
        }
    }

    previous_ = counters;
    previousTime_ = now;
    havePrevious_ = true;
    return out;
}

}  // namespace gscope
