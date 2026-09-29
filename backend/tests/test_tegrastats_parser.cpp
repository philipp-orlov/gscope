// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/tegrastats_provider.hpp"

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
    // Captured from a real Jetson Orin NX (JetPack 5 / L4T R35.4.1).
    const std::string line =
        "09-22-2026 00:29:21 RAM 4461/15524MB (lfb 196x4MB) SWAP 34/7762MB (cached 0MB) "
        "CPU [0%@1984,0%@1984,0%@1984,1%@1984,0%@1984,0%@1984,0%@1984,0%@1984] "
        "EMC_FREQ 0% GR3D_FREQ 0%@[0] CV0@51.093C CPU@52.75C SOC2@51.281C SOC0@52.125C "
        "CV1@51.187C GPU@50.656C tj@52.843C SOC1@52.781C CV2@50C";

    gscope::Sample sample;
    check(gscope::parseTegrastatsLine(line, sample), "line should parse");

    check(near(sample.memUsedMb, 4461), "RAM used");
    check(near(sample.memTotalMb, 15524), "RAM total");
    check(near(sample.swapUsedMb, 34), "SWAP used");
    check(near(sample.swapTotalMb, 7762), "SWAP total");

    check(sample.cpuCores.size() == 8, "8 CPU cores");
    check(near(sample.cpuCores[0].utilizationPercent, 0), "core0 util");
    check(near(sample.cpuCores[3].utilizationPercent, 1), "core3 util");
    check(near(sample.cpuCores[0].frequencyMhz, 1984), "core0 freq");

    check(sample.gpus.size() == 1, "one GPU");
    check(near(sample.gpus[0].utilizationPercent, 0), "GPU util");
    check(near(sample.gpus[0].temperatureC, 50.656), "GPU temp");
    check(near(sample.gpus[0].memoryTotalMb, 15524), "GPU memory mirrors system RAM");

    bool foundCpuTemp = false;
    for (auto& [name, value] : sample.temperatures) {
        if (name == "CPU" && near(value, 52.75))
            foundCpuTemp = true;
    }
    check(foundCpuTemp, "CPU temperature sensor");

    check(!gscope::parseTegrastatsLine("not a tegrastats line", sample), "garbage input rejected");

    std::puts("tegrastats parser: RAM/SWAP/CPU/GR3D/temperature fields passed");
    return 0;
}
