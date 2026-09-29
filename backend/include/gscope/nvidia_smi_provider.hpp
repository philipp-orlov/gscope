// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: discrete-GPU provider for machines with NVIDIA's driver but no
// Jetson specifics (a desktop/server dGPU box) -- the "hook" for running
// this backend off-Jetson while still reporting real GPU numbers, the way
// nvtop does by shelling out rather than linking NVML. CPU/RAM come from
// ProcCpuMemSampler since nvidia-smi doesn't report either.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gscope/line_stream.hpp"
#include "gscope/proc_provider.hpp"
#include "gscope/provider.hpp"

namespace gscope {

// Parses one CSV line from:
//   nvidia-smi --query-gpu=index,name,utilization.gpu,memory.used,
//              memory.total,temperature.gpu,power.draw,clocks.sm
//              --format=csv,noheader,nounits
// e.g. "0, NVIDIA GeForce RTX 4090, 12, 1024, 24564, 45, 32.10, 1785"
// Exposed for tests. Returns false if the line doesn't split into the
// expected 8 fields. memory.used/memory.total of [N/A] (unified-memory
// NVIDIA SoCs such as GB10/Grace-Blackwell) parse to NaN; NvidiaSmiProvider
// mirrors system RAM onto those fields before a Sample is emitted.
bool parseNvidiaSmiCsvLine(std::string_view line, GpuSample& out);

class NvidiaSmiProvider : public MetricsProvider
{
public:
    // streamFactory defaults to spawning the real `nvidia-smi --loop=`
    // query above; tests inject a factory that returns a VectorLineStream.
    // gpuCount tells the provider how many consecutive CSV lines make up
    // one sample (nvidia-smi with multiple GPUs prints one line per GPU,
    // back to back, every interval); 0 makes start() probe it via
    // `nvidia-smi --query-gpu=count --format=csv,noheader`.
    explicit NvidiaSmiProvider(unsigned intervalMs = 1000, unsigned gpuCount = 0,
                               LineStreamFactory streamFactory = {});
    ~NvidiaSmiProvider() override;

    const char* name() const override { return "nvidia-smi"; }
    bool start(SampleCallback callback) override;
    void stop() override;

private:
    void run(SampleCallback callback);

    unsigned intervalMs_;
    unsigned gpuCount_;
    LineStreamFactory streamFactory_;
    std::unique_ptr<LineStream> stream_;
    ProcCpuMemSampler cpuMemSampler_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

}  // namespace gscope
