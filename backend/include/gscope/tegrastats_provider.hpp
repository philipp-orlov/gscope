// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: Jetson provider, reading `tegrastats`' one-line-per-sample
// text output. Covers CPU per-core %/freq, RAM+SWAP, the single
// integrated GPU's utilization/temperature (memory is unified with system
// RAM on Jetson, so the GPU's memory fields mirror it) and every "NAME@N C"
// temperature sensor tegrastats prints. Fields tegrastats sometimes adds
// (power rails, EMC_FREQ, APE, MTS...) are ignored rather than rejected --
// the line format is not the same across every Jetson/L4T release, and a
// stray token here should not take the whole sample down.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "gscope/line_stream.hpp"
#include "gscope/provider.hpp"

namespace gscope {

// Pure parsing, exercised directly by tests with canned lines -- no
// process, no thread. Returns false (leaving `out` unspecified) if the
// line doesn't look like tegrastats output at all (missing "RAM"/"CPU").
bool parseTegrastatsLine(std::string_view line, Sample& out);

class TegrastatsProvider : public MetricsProvider
{
public:
    // streamFactory defaults to spawning the real `tegrastats` binary;
    // tests inject a factory that returns a VectorLineStream instead.
    explicit TegrastatsProvider(unsigned intervalMs = 1000, LineStreamFactory streamFactory = {});
    ~TegrastatsProvider() override;

    const char* name() const override { return "tegrastats"; }
    bool start(SampleCallback callback) override;
    void stop() override;

private:
    void run(SampleCallback callback);

    unsigned intervalMs_;
    LineStreamFactory streamFactory_;
    std::unique_ptr<LineStream> stream_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

}  // namespace gscope
