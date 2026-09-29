// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/nvidia_smi_provider.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "http/logger.hpp"

namespace gscope {
namespace {

std::string trim(std::string_view text)
{
    size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos)
        return {};
    size_t end = text.find_last_not_of(" \t");
    return std::string(text.substr(begin, end - begin + 1));
}

std::vector<std::string> splitCsv(std::string_view line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= line.size()) {
        size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields.push_back(trim(line.substr(start)));
            break;
        }
        fields.push_back(trim(line.substr(start, comma - start)));
        start = comma + 1;
    }
    return fields;
}

double parseDoubleOr(const std::string& text, double fallback)
{
    if (text.empty() || text == "[N/A]" || text == "N/A")
        return fallback;
    return std::strtod(text.c_str(), nullptr);
}

}  // namespace

bool parseNvidiaSmiCsvLine(std::string_view line, GpuSample& out)
{
    std::vector<std::string> fields = splitCsv(line);
    if (fields.size() != 8)
        return false;

    out = GpuSample{};
    out.name = fields[1];
    out.utilizationPercent = parseDoubleOr(fields[2], 0.0);
    // NaN (rather than 0) marks memory.used/memory.total as unreported so the
    // caller can tell "unified memory, mirror system RAM" apart from "a real
    // 0 MB reading" -- unified-memory NVIDIA SoCs (e.g. GB10/Grace-Blackwell)
    // report [N/A] for both, the same way Jetson's tegrastats has no separate
    // GPU memory counter.
    out.memoryUsedMb = parseDoubleOr(fields[3], std::nan(""));
    out.memoryTotalMb = parseDoubleOr(fields[4], std::nan(""));
    out.temperatureC = parseDoubleOr(fields[5], -1000.0);
    out.powerMw = parseDoubleOr(fields[6], -1.0) * 1000.0;  // nvidia-smi reports watts
    out.frequencyMhz = parseDoubleOr(fields[7], 0.0);
    return true;
}

namespace {
const char* kQueryFields =
    "index,name,utilization.gpu,memory.used,memory.total,temperature.gpu,power.draw,clocks.sm";
}

NvidiaSmiProvider::NvidiaSmiProvider(unsigned intervalMs, unsigned gpuCount,
                                   LineStreamFactory streamFactory)
    : intervalMs_(intervalMs), gpuCount_(gpuCount), streamFactory_(std::move(streamFactory))
{}

NvidiaSmiProvider::~NvidiaSmiProvider()
{
    stop();
}

bool NvidiaSmiProvider::start(SampleCallback callback)
{
    if (running_.exchange(true))
        return true;

    if (gpuCount_ == 0 && !streamFactory_) {
        // Probe the device count once so the read loop knows how many
        // consecutive CSV lines make up one sample.
        FILE* countPipe = popen("nvidia-smi --query-gpu=count --format=csv,noheader", "r");
        if (!countPipe) {
            running_ = false;
            return false;
        }
        char buffer[64] = {0};
        if (std::fgets(buffer, sizeof(buffer), countPipe))
            gpuCount_ = static_cast<unsigned>(std::strtoul(buffer, nullptr, 10));
        pclose(countPipe);
        if (gpuCount_ == 0) {
            running_ = false;
            return false;
        }
    } else if (gpuCount_ == 0) {
        gpuCount_ = 1;  // tests supply a stream factory; default to one GPU
    }

    stream_ = streamFactory_
                  ? streamFactory_()
                  : std::make_unique<PopenLineStream>(
                        "nvidia-smi --query-gpu=" + std::string(kQueryFields) +
                        " --format=csv,noheader,nounits -l " +
                        std::to_string(std::max(1u, intervalMs_ / 1000)));
    if (!stream_) {
        running_ = false;
        return false;
    }

    worker_ = std::thread(&NvidiaSmiProvider::run, this, std::move(callback));
    return true;
}

void NvidiaSmiProvider::stop()
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

void NvidiaSmiProvider::run(SampleCallback callback)
{
    // Read stream_'s pointer exactly once: stop() overwrites the member
    // concurrently from another thread (to interrupt a blocked read), so
    // re-reading it every iteration is a data race that can hand nextLine()
    // a null/torn pointer.
    LineStream* stream = stream_.get();
    std::string line;
    while (running_.load() && stream) {
        Sample sample = cpuMemSampler_.sample();
        bool ok = true;
        for (unsigned i = 0; i < gpuCount_ && ok; ++i) {
            if (!stream->nextLine(line)) {
                ok = false;
                break;
            }
            GpuSample gpu;
            if (parseNvidiaSmiCsvLine(line, gpu)) {
                // memory.used/memory.total came back [N/A] -- unified memory
                // (no separate GPU pool), so mirror system RAM the same way
                // TegrastatsProvider does for Jetson.
                if (std::isnan(gpu.memoryUsedMb))
                    gpu.memoryUsedMb = sample.memUsedMb;
                if (std::isnan(gpu.memoryTotalMb))
                    gpu.memoryTotalMb = sample.memTotalMb;
                // Surface each GPU's temperature in the generic sensor table
                // too (thermal_zone sysfs, read above, is often empty on
                // servers that lack ACPI thermal zones).
                if (gpu.temperatureC > -1000.0)
                    sample.temperatures.emplace_back("GPU " + std::to_string(i), gpu.temperatureC);
                sample.gpus.push_back(std::move(gpu));
            } else {
                HTTP_LOG_WARN("nvidia-smi: unrecognized line, skipped");
            }
        }
        if (!ok)
            break;
        callback(sample);
    }
}

}  // namespace gscope
