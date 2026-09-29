// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/tegrastats_provider.hpp"

#include <charconv>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <vector>

#include "http/logger.hpp"

namespace gscope {
namespace {

std::vector<std::string_view> split(std::string_view text, char delimiter)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(delimiter, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

double toDouble(std::string_view text)
{
    double value = 0.0;
    // std::from_chars(double) isn't available on the toolchains this
    // targets (libstdc++ < 11); std::strtod needs a NUL-terminated buffer.
    std::string owned(text);
    value = std::strtod(owned.c_str(), nullptr);
    return value;
}

// "0%@1984" -> {utilization=0, frequencyMhz=1984}
CpuCoreSample parseCpuCoreToken(std::string_view token)
{
    CpuCoreSample core;
    size_t at = token.find('@');
    std::string_view utilPart = at == std::string_view::npos ? token : token.substr(0, at);
    if (!utilPart.empty() && utilPart.back() == '%')
        utilPart.remove_suffix(1);
    core.utilizationPercent = toDouble(utilPart);
    if (at != std::string_view::npos)
        core.frequencyMhz = toDouble(token.substr(at + 1));
    return core;
}

// "RAM 4461/15524MB" -> used=4461, total=15524. `tokens[index]` is "RAM";
// the used/total sit in tokens[index + 1].
bool parseUsedTotalMb(const std::vector<std::string_view>& tokens, size_t valueIndex,
                     double& used, double& total)
{
    if (valueIndex >= tokens.size())
        return false;
    std::string_view value = tokens[valueIndex];
    size_t slash = value.find('/');
    size_t unit = value.find("MB");
    if (slash == std::string_view::npos || unit == std::string_view::npos || unit < slash)
        return false;
    used = toDouble(value.substr(0, slash));
    total = toDouble(value.substr(slash + 1, unit - slash - 1));
    return true;
}

}  // namespace

bool parseTegrastatsLine(std::string_view line, Sample& out)
{
    if (line.find("RAM") == std::string_view::npos || line.find("CPU") == std::string_view::npos)
        return false;

    out = Sample{};
    out.timestamp = std::chrono::system_clock::now();

    std::vector<std::string_view> tokens = split(line, ' ');
    // Tokens with an internal space -- "(lfb 196x4MB)", "(cached 0MB)" --
    // are informational only and skipped by index below rather than
    // reassembled.

    double gpuUtilization = -1.0;
    bool haveGpuUtilization = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        std::string_view token = tokens[i];

        if (token == "RAM") {
            parseUsedTotalMb(tokens, i + 1, out.memUsedMb, out.memTotalMb);
        } else if (token == "SWAP") {
            parseUsedTotalMb(tokens, i + 1, out.swapUsedMb, out.swapTotalMb);
        } else if (token == "CPU" && i + 1 < tokens.size() && !tokens[i + 1].empty() &&
                  tokens[i + 1].front() == '[') {
            std::string_view list = tokens[i + 1];
            list.remove_prefix(1);
            if (!list.empty() && list.back() == ']')
                list.remove_suffix(1);
            for (std::string_view coreToken : split(list, ','))
                out.cpuCores.push_back(parseCpuCoreToken(coreToken));
        } else if (token == "GR3D_FREQ" && i + 1 < tokens.size()) {
            std::string_view value = tokens[i + 1];
            size_t percent = value.find('%');
            if (percent != std::string_view::npos) {
                gpuUtilization = toDouble(value.substr(0, percent));
                haveGpuUtilization = true;
            }
        } else {
            size_t at = token.find('@');
            if (at != std::string_view::npos && !token.empty() && token.back() == 'C') {
                std::string_view sensorName = token.substr(0, at);
                std::string_view sensorValue = token.substr(at + 1, token.size() - at - 2);
                out.temperatures.emplace_back(std::string(sensorName), toDouble(sensorValue));
            }
        }
    }

    if (haveGpuUtilization) {
        GpuSample gpu;
        gpu.name = "GPU0";
        gpu.utilizationPercent = gpuUtilization;
        // Jetson's GPU shares system RAM (unified memory) -- there is no
        // separate GPU memory counter to report.
        gpu.memoryUsedMb = out.memUsedMb;
        gpu.memoryTotalMb = out.memTotalMb;
        for (auto& [sensorName, sensorValue] : out.temperatures) {
            if (sensorName == "GPU") {
                gpu.temperatureC = sensorValue;
                break;
            }
        }
        out.gpus.push_back(std::move(gpu));
    }

    return true;
}

TegrastatsProvider::TegrastatsProvider(unsigned intervalMs, LineStreamFactory streamFactory)
    : intervalMs_(intervalMs), streamFactory_(std::move(streamFactory))
{}

TegrastatsProvider::~TegrastatsProvider()
{
    stop();
}

bool TegrastatsProvider::start(SampleCallback callback)
{
    if (running_.exchange(true))
        return true;

    stream_ = streamFactory_
                  ? streamFactory_()
                  : std::make_unique<PopenLineStream>(
                        "tegrastats --interval " + std::to_string(intervalMs_));
    if (!stream_) {
        running_ = false;
        return false;
    }

    worker_ = std::thread(&TegrastatsProvider::run, this, std::move(callback));
    return true;
}

void TegrastatsProvider::stop()
{
    if (!running_.exchange(false))
        return;
    // Cancel (interrupt a blocked read) and join the reader thread before
    // destroying the stream -- destroying it first races the thread's
    // still-in-flight nextLine() call (heap-use-after-free).
    if (stream_)
        stream_->cancel();
    if (worker_.joinable())
        worker_.join();
    stream_.reset();
}

void TegrastatsProvider::run(SampleCallback callback)
{
    std::string line;
    while (running_.load() && stream_ && stream_->nextLine(line)) {
        Sample sample;
        if (parseTegrastatsLine(line, sample))
            callback(sample);
        else
            HTTP_LOG_WARN("tegrastats: unrecognized line, skipped");
    }
}

}  // namespace gscope
