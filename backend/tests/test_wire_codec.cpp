// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/wire_codec.hpp"

#include "fake_provider.hpp"

using gscope_test::check;

int main()
{
    // Fixed point at the dashboard's precision, clamped to the field.
    check(gscope::toCentiPercent(12.345) == 1235, "percent in hundredths, rounded");
    check(gscope::toCentiPercent(250) == 10000 && gscope::toCentiPercent(-3) == 0, "percent clamped to 0..100");
    check(gscope::toCentiPercentWide(350.5) == 35050, "a process on several cores goes past 100 %");
    check(gscope::toDeciCelsius(41.26) == 413, "temperature in tenths");
    check(gscope::toDeciCelsius(-1000) == gscope::kNoTemperature, "the providers' -1000 sentinel is no reading");
    check(gscope::toKib(1.5) == 1536, "MiB to KiB");
    check(gscope::toMhz(1e9) == 65535, "MHz clamped");

    gscope::Sample sample = gscope_test::makeSample(10.0);
    sample.processes.push_back({42, "worker", 150.25, 2.5});
    sample.temperatures.push_back({"SOC", 60.0});
    sample.network = {1.5e6, 2.5e5};
    const gscope::Inventory inventory = gscope::inventoryOf(sample, gscope_test::testHost(), "fake", "b1");
    check(inventory.sensors == std::vector<std::string>{"CPU", "SOC"}, "inventory lists the sensors in order");
    check(inventory.gpus.size() == 1 && inventory.gpus[0].memoryTotalMib == 122880, "inventory holds GPU totals");

    const bufs::Message message = gscope::encodeUpdate(&inventory, inventory, &sample, 1);
    check(bufs::verify<gscope::wire::Update>(message.data(), message.size()), "the update verifies");
    auto update = bufs::root<gscope::wire::Update>(message.data());
    auto inv = update.inventory();
    check(inv && inv.hostname() == "test-host" && inv.provider() == "fake" && inv.buildId() == "b1" &&
              inv.cpuCount() == 20 && inv.memTotalMib() == 8000,
          "inventory fields");
    check(inv.gpus().size() == 1 && inv.gpus()[0].name() == "NVIDIA GB10", "GPU names travel in the inventory");
    check(inv.sensors().size() == 2 && inv.sensors()[1] == "SOC", "sensor names travel in the inventory");
    check(update.samples().size() == 1, "one sample");
    auto s = update.samples()[0];
    check(s.cores().size() == 1 && s.cores()[0].utilizationCenti == 1000 && s.cores()[0].frequencyMhz == 1500, "cores");
    check(s.memUsedMib() == 1000 && s.rxBytesPerSec() == 1500000 && s.txBytesPerSec() == 250000, "memory and network");
    const gscope::wire::Gpu& gpu = s.gpus()[0];
    check(gpu.utilizationCenti == 1235 && gpu.memoryUsedMib == 4096 && gpu.temperatureDeci == 413 &&
              gpu.powerMw == 5600 && gpu.frequencyMhz == 1800,
          "GPU reading");
    check(s.temperaturesDeci().size() == 2 && s.temperaturesDeci()[0] == 500 && s.temperaturesDeci()[1] == 600,
          "temperatures at the inventory's indices");
    check(s.processes().size() == 1 && s.processes()[0].pid() == 42 && s.processes()[0].name() == "worker" &&
              s.processes()[0].cpuCenti() == 15025 && s.processes()[0].memoryKib() == 2560,
          "processes");

    // Without an inventory, and a sample whose sensors moved: matched by name.
    gscope::Sample moved = sample;
    moved.temperatures = {{"SOC", 61.0}, {"NEW", 1.0}};
    const bufs::Message bare = gscope::encodeUpdate(nullptr, inventory, &moved, 1);
    auto bareUpdate = bufs::root<gscope::wire::Update>(bare.data());
    check(!bareUpdate.inventory(), "an update without an inventory has none");
    auto t = bareUpdate.samples()[0].temperaturesDeci();
    check(t.size() == 2 && t[0] == gscope::kNoTemperature && t[1] == 610,
          "a missing sensor is no reading, a known one is found by name, an unknown one is left out");
    check(bare.size() < message.size(), "a regular update is smaller than one with the inventory");

    std::printf("wire codec: %zu bytes with the inventory, %zu without\n", message.size(), bare.size());
    return 0;
}
