// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/proc_provider.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace gscope {
namespace {

std::string readFile(const char* path)
{
    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// /sys/class/thermal/thermal_zone<N>/{type,temp} -- generic Linux sensor
// framework, present regardless of which MetricsProvider is active (unlike
// tegrastats' own temperature fields, which only exist on Jetson). "type"
// is a short name ("acpitz", "x86_pkg_temp", ...); "temp" is millidegrees C.
// Missing/unreadable sysfs (containers, some ARM boards) just yields no
// entries rather than an error -- temperatures is allowed to be empty.
void readThermalZones(Sample& out)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator entries("/sys/class/thermal", ec);
    if (ec)
        return;

    for (const auto& entry : entries) {
        std::string zone = entry.path().filename().string();
        if (zone.rfind("thermal_zone", 0) != 0)
            continue;

        std::string type = readFile((entry.path() / "type").c_str());
        while (!type.empty() && (type.back() == '\n' || type.back() == '\r'))
            type.pop_back();
        if (type.empty())
            continue;

        std::istringstream tempStream(readFile((entry.path() / "temp").c_str()));
        long milliC = 0;
        if (!(tempStream >> milliC))
            continue;

        out.temperatures.emplace_back(type + " " + zone.substr(std::strlen("thermal_zone")),
                                      milliC / 1000.0);
    }
}

// One entry per core, oldest-index-first to match /proc/stat's "cpuN"
// ordering. Prefers /sys/.../cpufreq/scaling_cur_freq (kHz, updated live by
// the active cpufreq governor); falls back to /proc/cpuinfo's "cpu MHz"
// column (present even without a cpufreq driver, e.g. some VMs/containers)
// for any core missing the sysfs file. 0 if neither source has a reading.
std::vector<double> readCpuCoreFrequenciesMhz(size_t coreCount)
{
    std::vector<double> frequencies(coreCount, 0.0);

    bool anyMissing = false;
    for (size_t i = 0; i < coreCount; ++i) {
        std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_cur_freq";
        std::string content = readFile(path.c_str());
        double khz = content.empty() ? 0.0 : std::strtod(content.c_str(), nullptr);
        if (khz > 0.0)
            frequencies[i] = khz / 1000.0;
        else
            anyMissing = true;
    }
    if (!anyMissing)
        return frequencies;

    std::istringstream lines(readFile("/proc/cpuinfo"));
    std::string line;
    size_t index = 0;
    while (index < coreCount && std::getline(lines, line)) {
        if (line.rfind("cpu MHz", 0) != 0)
            continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        if (frequencies[index] <= 0.0)
            frequencies[index] = std::strtod(line.c_str() + colon + 1, nullptr);
        ++index;
    }
    return frequencies;
}

}  // namespace

ProcCpuTimes parseProcStat(const std::string& content)
{
    ProcCpuTimes times;
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        // "cpu0 <user> <nice> <system> <idle> <iowait> <irq> <softirq> ..."
        // Skip the aggregate "cpu " line -- only per-core lines are kept,
        // one Sample.cpuCores entry each.
        if (line.rfind("cpu", 0) != 0 || line.size() < 4 || !std::isdigit(line[3]))
            continue;

        std::istringstream fields(line);
        std::string label;
        fields >> label;
        std::vector<uint64_t> values;
        uint64_t value = 0;
        while (fields >> value)
            values.push_back(value);
        if (values.size() < 4)
            continue;

        ProcCpuTimes::Core core;
        uint64_t total = 0;
        for (uint64_t v : values)
            total += v;
        core.total = total;
        core.idle = values[3];  // idle; values[4] (iowait) is also idle time but kept out on purpose to match /proc/stat's canonical "idle" column
        times.cores.push_back(core);
    }
    return times;
}

void parseProcMeminfo(const std::string& content, Sample& out)
{
    std::istringstream lines(content);
    std::string line;
    double memTotalKb = 0, memAvailableKb = 0, swapTotalKb = 0, swapFreeKb = 0;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string label;
        double value = 0;
        fields >> label >> value;
        if (label == "MemTotal:")
            memTotalKb = value;
        else if (label == "MemAvailable:")
            memAvailableKb = value;
        else if (label == "SwapTotal:")
            swapTotalKb = value;
        else if (label == "SwapFree:")
            swapFreeKb = value;
    }
    out.memTotalMb = memTotalKb / 1024.0;
    out.memUsedMb = (memTotalKb - memAvailableKb) / 1024.0;
    out.swapTotalMb = swapTotalKb / 1024.0;
    out.swapUsedMb = (swapTotalKb - swapFreeKb) / 1024.0;
}

std::vector<double> cpuUtilizationPercent(const ProcCpuTimes& previous, const ProcCpuTimes& current)
{
    std::vector<double> percentages;
    size_t coreCount = std::min(previous.cores.size(), current.cores.size());
    percentages.reserve(coreCount);
    for (size_t i = 0; i < coreCount; ++i) {
        uint64_t totalDelta = current.cores[i].total - previous.cores[i].total;
        uint64_t idleDelta = current.cores[i].idle - previous.cores[i].idle;
        double percent = totalDelta == 0
                             ? 0.0
                             : 100.0 * static_cast<double>(totalDelta - idleDelta) /
                                   static_cast<double>(totalDelta);
        percentages.push_back(percent);
    }
    return percentages;
}

Sample ProcCpuMemSampler::sample()
{
    Sample out;
    out.timestamp = std::chrono::system_clock::now();

    ProcCpuTimes current = parseProcStat(readFile("/proc/stat"));
    std::vector<double> frequencies = readCpuCoreFrequenciesMhz(current.cores.size());
    if (havePrevious_) {
        std::vector<double> percentages = cpuUtilizationPercent(previous_, current);
        for (size_t i = 0; i < percentages.size(); ++i)
            out.cpuCores.push_back(CpuCoreSample{percentages[i], frequencies[i]});
    } else {
        for (double frequencyMhz : frequencies)
            out.cpuCores.push_back(CpuCoreSample{0.0, frequencyMhz});
    }
    previous_ = current;
    havePrevious_ = true;

    parseProcMeminfo(readFile("/proc/meminfo"), out);
    readThermalZones(out);
    return out;
}

ProcFallbackProvider::~ProcFallbackProvider()
{
    stop();
}

bool ProcFallbackProvider::start(SampleCallback callback)
{
    if (running_.exchange(true))
        return true;
    worker_ = std::thread([this, callback = std::move(callback)]() mutable {
        ProcCpuMemSampler sampler;
        while (running_.load()) {
            callback(sampler.sample());
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_));
        }
    });
    return true;
}

void ProcFallbackProvider::stop()
{
    if (!running_.exchange(false))
        return;
    if (worker_.joinable())
        worker_.join();
}

}  // namespace gscope
