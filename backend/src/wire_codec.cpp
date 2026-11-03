// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/wire_codec.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace gscope {
namespace {

template <class T>
T clampRound(double value, double low, double high)
{
    if (!std::isfinite(value))
        return static_cast<T>(low);
    return static_cast<T>(std::llround(std::clamp(value, low, high)));
}

void fillInventory(wire::Inventory::Builder& out, const Inventory& inventory)
{
    out.setHostname(inventory.host.hostname);
    out.setKernel(inventory.host.kernel);
    out.setProvider(inventory.provider);
    out.setCpuCount(inventory.host.cpuCount);
    out.setBuildId(inventory.buildId);
    out.setMemTotalMib(inventory.memTotalMib);
    out.setSwapTotalMib(inventory.swapTotalMib);
    auto gpus = out.initGpus(inventory.gpus.size());
    for (size_t i = 0; i < inventory.gpus.size(); ++i) {
        auto gpu = gpus.init(i);
        gpu.setName(inventory.gpus[i].name);
        gpu.setMemoryTotalMib(inventory.gpus[i].memoryTotalMib);
    }
    auto sensors = out.initSensors(inventory.sensors.size());
    for (size_t i = 0; i < inventory.sensors.size(); ++i)
        sensors.set(i, inventory.sensors[i]);
}

void fillSample(wire::Sample::Builder& out, const Sample& sample, const Inventory& layout)
{
    using namespace std::chrono;
    out.setTimestampMs(duration_cast<milliseconds>(sample.timestamp.time_since_epoch()).count());
    out.setMemUsedMib(toMib(sample.memUsedMb));
    out.setSwapUsedMib(toMib(sample.swapUsedMb));
    out.setRxBytesPerSec(clampRound<uint64_t>(sample.network.rxBytesPerSec, 0, 9.0e18));
    out.setTxBytesPerSec(clampRound<uint64_t>(sample.network.txBytesPerSec, 0, 9.0e18));

    auto cores = out.initCores(sample.cpuCores.size());
    for (size_t i = 0; i < sample.cpuCores.size(); ++i) {
        wire::Core core;
        core.utilizationCenti = toCentiPercent(sample.cpuCores[i].utilizationPercent);
        core.frequencyMhz = toMhz(sample.cpuCores[i].frequencyMhz);
        cores.set(i, core);
    }

    auto gpus = out.initGpus(sample.gpus.size());
    for (size_t i = 0; i < sample.gpus.size(); ++i) {
        const GpuSample& from = sample.gpus[i];
        wire::Gpu gpu;
        gpu.memoryUsedMib = toMib(from.memoryUsedMb);
        gpu.powerMw = from.powerMw < 0 ? -1 : clampRound<int32_t>(from.powerMw, 0, 2147483647.0);
        gpu.utilizationCenti = toCentiPercent(from.utilizationPercent);
        gpu.frequencyMhz = toMhz(from.frequencyMhz);
        gpu.temperatureDeci = toDeciCelsius(from.temperatureC);
        gpus.set(i, gpu);
    }

    // At the index of the layout's sensor of the same name: the order a
    // provider lists them in is stable, so this is almost always i == j.
    auto temperatures = out.initTemperaturesDeci(layout.sensors.size());
    for (size_t j = 0; j < layout.sensors.size(); ++j) {
        int16_t value = kNoTemperature;
        if (j < sample.temperatures.size() && sample.temperatures[j].first == layout.sensors[j]) {
            value = toDeciCelsius(sample.temperatures[j].second);
        } else {
            for (const auto& [name, celsius] : sample.temperatures)
                if (name == layout.sensors[j]) {
                    value = toDeciCelsius(celsius);
                    break;
                }
        }
        temperatures.set(j, value);
    }

    auto processes = out.initProcesses(sample.processes.size());
    for (size_t i = 0; i < sample.processes.size(); ++i) {
        const ProcessSample& from = sample.processes[i];
        auto process = processes.init(i);
        process.setPid(from.pid);
        process.setCpuCenti(toCentiPercentWide(from.cpuPercent));
        process.setMemoryKib(toKib(from.memoryMb));
        process.setName(from.name);
    }
}

}  // namespace

bool Inventory::operator==(const Inventory& other) const
{
    return host == other.host && provider == other.provider && buildId == other.buildId &&
           memTotalMib == other.memTotalMib && swapTotalMib == other.swapTotalMib && gpus == other.gpus &&
           sensors == other.sensors;
}

Inventory inventoryOf(const Sample& sample, const HostInfo& host, std::string provider, std::string buildId)
{
    Inventory inventory;
    inventory.host = host;
    inventory.provider = std::move(provider);
    inventory.buildId = std::move(buildId);
    inventory.memTotalMib = toMib(sample.memTotalMb);
    inventory.swapTotalMib = toMib(sample.swapTotalMb);
    for (const GpuSample& gpu : sample.gpus)
        inventory.gpus.push_back({gpu.name, toMib(gpu.memoryTotalMb)});
    for (const auto& sensor : sample.temperatures)
        inventory.sensors.push_back(sensor.first);
    return inventory;
}

uint16_t toCentiPercent(double percent)
{
    return clampRound<uint16_t>(percent * 100.0, 0, 10000);
}

uint32_t toCentiPercentWide(double percent)
{
    return clampRound<uint32_t>(percent * 100.0, 0, 4294967295.0);
}

int16_t toDeciCelsius(double celsius)
{
    if (!std::isfinite(celsius) || celsius <= -900.0)
        return kNoTemperature;
    return clampRound<int16_t>(celsius * 10.0, -32767, 32767);
}

uint32_t toMib(double mib)
{
    return clampRound<uint32_t>(mib, 0, 4294967295.0);
}

uint32_t toKib(double mib)
{
    return clampRound<uint32_t>(mib * 1024.0, 0, 4294967295.0);
}

uint16_t toMhz(double mhz)
{
    return clampRound<uint16_t>(mhz, 0, 65535);
}

bufs::Message encodeUpdate(const Inventory* inventory, const Inventory& layout, const Sample* samples, size_t count)
{
    bufs::MessageBuilder builder;
    auto update = builder.initRoot<wire::Update>();
    if (inventory) {
        auto out = update.initInventory();
        fillInventory(out, *inventory);
    }
    auto list = update.initSamples(count);
    for (size_t i = 0; i < count; ++i) {
        auto out = list.init(i);
        fillSample(out, samples[i], layout);
    }
    return builder.release();
}

}  // namespace gscope
