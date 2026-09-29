// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/process_sampler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>

namespace gscope {
namespace {

std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::vector<int> listPids()
{
    std::vector<int> pids;
    std::error_code errorCode;
    // Non-throwing form: pids exit between listing and iterating routinely
    // on a busy box, and that race is not an error worth reporting.
    for (auto it = std::filesystem::directory_iterator("/proc", errorCode);
         !errorCode && it != std::filesystem::directory_iterator(); it.increment(errorCode)) {
        const std::string name = it->path().filename().string();
        if (!name.empty() && std::all_of(name.begin(), name.end(), ::isdigit))
            pids.push_back(std::atoi(name.c_str()));
    }
    return pids;
}

}  // namespace

bool parseProcPidStat(std::string_view content, ProcPidStat& out)
{
    size_t open = content.find('(');
    size_t close = content.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open)
        return false;

    out.pid = std::atoi(std::string(content.substr(0, open)).c_str());
    out.name = std::string(content.substr(open + 1, close - open - 1));

    // Fields after the name: state(3) ppid(4) pgrp(5) session(6) tty_nr(7)
    // tpgid(8) flags(9) minflt(10) cminflt(11) majflt(12) cmajflt(13)
    // utime(14) stime(15) -- i.e. utime/stime are the 12th/13th token
    // (0-indexed 11/12) counting from "state" right after the name.
    std::string_view rest = content.substr(std::min(close + 2, content.size()));
    std::istringstream fields{std::string(rest)};
    std::string token;
    std::vector<std::string> tokens;
    while (fields >> token) tokens.push_back(token);
    if (tokens.size() < 13)
        return false;

    out.utimeTicks = std::strtoull(tokens[11].c_str(), nullptr, 10);
    out.stimeTicks = std::strtoull(tokens[12].c_str(), nullptr, 10);
    return true;
}

double parseVmRssKb(std::string_view statusContent)
{
    std::istringstream lines{std::string(statusContent)};
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream fields(line);
            std::string label;
            double value = 0;
            fields >> label >> value;
            return value;
        }
    }
    return 0.0;
}

double cpuPercentFromTicks(uint64_t previousTicks, uint64_t currentTicks, double elapsedSeconds,
                           long ticksPerSecond)
{
    if (elapsedSeconds <= 0.0 || currentTicks < previousTicks || ticksPerSecond <= 0)
        return 0.0;
    uint64_t deltaTicks = currentTicks - previousTicks;
    return 100.0 * static_cast<double>(deltaTicks) / (static_cast<double>(ticksPerSecond) * elapsedSeconds);
}

std::vector<ProcessSample> ProcessSampler::sample(size_t topN)
{
    const auto now = std::chrono::steady_clock::now();
    const double elapsedSeconds =
        havePrevious_ ? std::chrono::duration<double>(now - previousTime_).count() : 0.0;
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);

    std::unordered_map<int, uint64_t> currentTicks;
    std::vector<ProcessSample> processes;

    for (int pid : listPids()) {
        ProcPidStat stat;
        if (!parseProcPidStat(readFile("/proc/" + std::to_string(pid) + "/stat"), stat))
            continue;  // process exited between listing and reading

        uint64_t ticks = stat.utimeTicks + stat.stimeTicks;
        currentTicks[pid] = ticks;

        ProcessSample process;
        process.pid = pid;
        process.name = stat.name;
        process.memoryMb = parseVmRssKb(readFile("/proc/" + std::to_string(pid) + "/status")) / 1024.0;
        if (havePrevious_) {
            auto previous = previousTicks_.find(pid);
            if (previous != previousTicks_.end())
                process.cpuPercent =
                    cpuPercentFromTicks(previous->second, ticks, elapsedSeconds, ticksPerSecond);
        }
        processes.push_back(std::move(process));
    }

    previousTicks_ = std::move(currentTicks);
    previousTime_ = now;
    havePrevious_ = true;

    std::sort(processes.begin(), processes.end(),
              [](const ProcessSample& a, const ProcessSample& b) { return a.cpuPercent > b.cpuPercent; });
    if (processes.size() > topN)
        processes.resize(topN);
    return processes;
}

}  // namespace gscope
