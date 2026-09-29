// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/proc_provider.hpp"

#include <cmath>
#include <cstdio>

namespace {
void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

bool near(double a, double b, double eps = 0.01)
{
    return std::fabs(a - b) < eps;
}
}  // namespace

int main()
{
    // user nice system idle iowait irq softirq steal guest guest_nice
    const std::string statA =
        "cpu  100 0 50 850 0 0 0 0 0 0\n"
        "cpu0 50 0 25 425 0 0 0 0 0 0\n"
        "cpu1 50 0 25 425 0 0 0 0 0 0\n"
        "intr 12345\n";
    const std::string statB =
        "cpu  200 0 100 900 0 0 0 0 0 0\n"
        "cpu0 120 0 60 430 0 0 0 0 0 0\n"  // busy delta 105, total delta 110 -> ~95.45%
        "cpu1 80 0 40 470 0 0 0 0 0 0\n"   // busy delta 45, total delta 90 -> 50%
        "intr 12400\n";

    gscope::ProcCpuTimes previous = gscope::parseProcStat(statA);
    gscope::ProcCpuTimes current = gscope::parseProcStat(statB);
    check(previous.cores.size() == 2, "two per-core lines parsed (aggregate 'cpu' line excluded)");

    std::vector<double> utilization = gscope::cpuUtilizationPercent(previous, current);
    check(utilization.size() == 2, "one utilization value per core");
    check(near(utilization[0], 95.45, 0.1), "core0 utilization");
    check(near(utilization[1], 50.0, 0.1), "core1 utilization");

    const std::string meminfo =
        "MemTotal:       16000000 kB\n"
        "MemFree:         2000000 kB\n"
        "MemAvailable:    4000000 kB\n"
        "SwapTotal:       8000000 kB\n"
        "SwapFree:        7000000 kB\n";
    gscope::Sample sample;
    gscope::parseProcMeminfo(meminfo, sample);
    check(near(sample.memTotalMb, 16000000.0 / 1024.0), "mem total MB");
    check(near(sample.memUsedMb, (16000000.0 - 4000000.0) / 1024.0), "mem used MB (total - available)");
    check(near(sample.swapTotalMb, 8000000.0 / 1024.0), "swap total MB");
    check(near(sample.swapUsedMb, (8000000.0 - 7000000.0) / 1024.0), "swap used MB");

    // First sample() call after construction has nothing to diff against,
    // so every core reads 0% rather than a garbage huge percentage.
    gscope::ProcCpuMemSampler sampler;
    gscope::Sample first = sampler.sample();
    for (const gscope::CpuCoreSample& core : first.cpuCores)
        check(near(core.utilizationPercent, 0.0), "first reading is 0% per core");

    // Light integration check against the real /sys/class/thermal: not
    // every machine/container has thermal zones, so an empty result is
    // fine, but whatever is reported must be a named, plausible reading.
    for (auto& [name, celsius] : first.temperatures) {
        check(!name.empty(), "thermal zone has a name");
        check(celsius > -50.0 && celsius < 150.0, "thermal zone reading is plausible");
    }

    std::puts("proc provider: /proc/stat and /proc/meminfo parsing passed");
    return 0;
}
