// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/nvidia_smi_provider.hpp"

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
    gscope::GpuSample gpu;
    check(gscope::parseNvidiaSmiCsvLine(
              "0, NVIDIA GeForce RTX 4090, 12, 1024, 24564, 45, 32.10, 1785", gpu),
          "line should parse");
    check(gpu.name == "NVIDIA GeForce RTX 4090", "name");
    check(near(gpu.utilizationPercent, 12), "utilization");
    check(near(gpu.memoryUsedMb, 1024), "memory used");
    check(near(gpu.memoryTotalMb, 24564), "memory total");
    check(near(gpu.temperatureC, 45), "temperature");
    check(near(gpu.powerMw, 32100), "power (W -> mW)");
    check(near(gpu.frequencyMhz, 1785), "frequency");

    // A second GPU: N/A fields (e.g. power on a headless/idle card) fall
    // back to sentinels instead of failing the whole sample.
    gscope::GpuSample gpu2;
    check(gscope::parseNvidiaSmiCsvLine("1, NVIDIA A100, 0, 0, 40960, 30, [N/A], 210", gpu2),
          "second GPU line should parse");
    check(near(gpu2.powerMw, -1000), "N/A power falls back to sentinel");

    // A unified-memory NVIDIA SoC (e.g. GB10/Grace-Blackwell): nvidia-smi
    // has no separate GPU memory pool to report, so both memory fields come
    // back [N/A]. These parse to NaN so NvidiaSmiProvider can tell "mirror
    // system RAM" apart from a genuine 0 MB reading.
    gscope::GpuSample gpu3;
    check(gscope::parseNvidiaSmiCsvLine("0, NVIDIA GB10, 0, [N/A], [N/A], 36, 5.38, 208", gpu3),
          "unified-memory GPU line should parse");
    check(std::isnan(gpu3.memoryUsedMb), "N/A memory.used parses to NaN");
    check(std::isnan(gpu3.memoryTotalMb), "N/A memory.total parses to NaN");

    check(!gscope::parseNvidiaSmiCsvLine("not,enough,fields", gpu), "malformed line rejected");

    std::puts("nvidia-smi parser: CSV fields and N/A fallback passed");
    return 0;
}
