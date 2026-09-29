// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/json_codec.hpp"

#include <chrono>
#include <cmath>

namespace gscope {
namespace {

// Percent fields (CPU/GPU utilization, per-process CPU%) come from
// providers with far more precision than is meaningful (e.g.
// 0.9998617421178851); round to 2 decimal places once here, the single
// place a Sample becomes JSON, so every consumer (REST, WS backlog, live
// broadcast) sees the same rounded value.
double roundPercent(double percent)
{
    return std::round(percent * 100.0) / 100.0;
}

}  // namespace

json::Json sampleToJson(const Sample& sample)
{
    using namespace std::chrono;

    json::Json cores = json::Json::array();
    for (const CpuCoreSample& core : sample.cpuCores) {
        cores.push_back(json::Json{
            {"utilization", roundPercent(core.utilizationPercent)},
            {"frequencyMhz", core.frequencyMhz},
        });
    }

    json::Json gpus = json::Json::array();
    for (const GpuSample& gpu : sample.gpus) {
        gpus.push_back(json::Json{
            {"name", gpu.name},
            {"utilization", roundPercent(gpu.utilizationPercent)},
            {"memoryUsedMb", gpu.memoryUsedMb},
            {"memoryTotalMb", gpu.memoryTotalMb},
            {"temperatureC", gpu.temperatureC},
            {"powerMw", gpu.powerMw},
            {"frequencyMhz", gpu.frequencyMhz},
        });
    }

    json::Json temperatures = json::Json::array();
    for (auto& [sensorName, celsius] : sample.temperatures) {
        temperatures.push_back(json::Json{{"name", sensorName}, {"celsius", celsius}});
    }

    json::Json processes = json::Json::array();
    for (const ProcessSample& process : sample.processes) {
        processes.push_back(json::Json{
            {"pid", process.pid},
            {"name", process.name},
            {"cpu", roundPercent(process.cpuPercent)},
            {"memoryMb", process.memoryMb},
        });
    }

    int64_t timestampMs =
        duration_cast<milliseconds>(sample.timestamp.time_since_epoch()).count();

    return json::Json{
        {"timestamp", static_cast<double>(timestampMs)},
        {"cpu", json::Json{{"cores", cores}}},
        {"memory", json::Json{
                       {"usedMb", sample.memUsedMb},
                       {"totalMb", sample.memTotalMb},
                       {"swapUsedMb", sample.swapUsedMb},
                       {"swapTotalMb", sample.swapTotalMb},
                   }},
        {"gpus", gpus},
        {"processes", processes},
        {"network", json::Json{
                        {"rxBytesPerSec", sample.network.rxBytesPerSec},
                        {"txBytesPerSec", sample.network.txBytesPerSec},
                    }},
        {"temperatures", temperatures},
    };
}

}  // namespace gscope
