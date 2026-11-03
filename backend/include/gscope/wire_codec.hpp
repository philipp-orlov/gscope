// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: the one place a Sample becomes a wire message (monitor.bufs), so
// the live updates, the backlog and the tests agree on the encoding.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "bufs/bufs.hpp"
#include "gscope/host_info.hpp"
#include "gscope/metrics.hpp"
#include "monitor.bufs.hpp"

namespace gscope {

// Gpu.temperature_deci / Sample.temperatures_deci when there is no reading.
constexpr int16_t kNoTemperature = std::numeric_limits<int16_t>::min();

// What the numbers of a sample refer to: the wire Inventory, as plain values
// that can be compared, so an update carries it only when it changed.
struct Inventory
{
    struct Gpu
    {
        std::string name;
        uint32_t memoryTotalMib = 0;
        bool operator==(const Gpu& other) const { return name == other.name && memoryTotalMib == other.memoryTotalMib; }
    };

    HostInfo host;
    std::string provider;
    std::string buildId;
    uint32_t memTotalMib = 0;
    uint32_t swapTotalMib = 0;
    std::vector<Gpu> gpus;
    std::vector<std::string> sensors;

    bool operator==(const Inventory& other) const;
    bool operator!=(const Inventory& other) const { return !(*this == other); }
};

// The inventory of a sample: its GPUs and sensors in order, its memory totals.
Inventory inventoryOf(const Sample& sample, const HostInfo& host, std::string provider, std::string buildId);

// Fixed point, at the precision the dashboard shows, clamped to the field.
uint16_t toCentiPercent(double percent);         // 0.01 %, 0..100 %
uint32_t toCentiPercentWide(double percent);     // 0.01 %, a process on many cores goes past 100 %
int16_t toDeciCelsius(double celsius);           // 0.1 degC; kNoTemperature below -900 (the providers' sentinel)
uint32_t toMib(double mib);
uint32_t toKib(double mib);
uint16_t toMhz(double mhz);

// One Update: `inventory` when not null, then `count` samples, oldest first.
// A sample's GPUs are matched to the inventory by index, its sensors by name
// (a sensor the inventory lacks is left out, one the sample lacks is
// kNoTemperature).
bufs::Message encodeUpdate(const Inventory* inventory, const Inventory& layout, const Sample* samples, size_t count);

}  // namespace gscope
