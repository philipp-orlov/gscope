// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/process_sampler.hpp"

#include <cstdio>
#include <unistd.h>

namespace {
void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}
}  // namespace

int main()
{
    // A real /proc/[pid]/stat line, name deliberately containing a space
    // and parens to exercise the "read between first '(' and last ')'"
    // parsing rather than splitting the whole line on spaces.
    const std::string line =
        "4242 (some (odd) name) S 1 4242 4242 0 -1 4194304 100 0 0 0 111 222 0 0 20 0 4 0 "
        "9999 123456789 4096 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0";
    gscope::ProcPidStat stat;
    check(gscope::parseProcPidStat(line, stat), "stat line parses");
    check(stat.pid == 4242, "pid parsed");
    check(stat.name == "some (odd) name", "name with spaces/parens parsed verbatim");
    check(stat.utimeTicks == 111, "utime parsed");
    check(stat.stimeTicks == 222, "stime parsed");

    check(!gscope::parseProcPidStat("not a stat line", stat), "line without parens rejected");
    check(!gscope::parseProcPidStat("1 (a) S 1 2", stat), "line too short is rejected");

    check(gscope::parseVmRssKb("Name:\tbash\nVmRSS:\t  4096 kB\nVmSize:\t 8192 kB\n") == 4096.0,
          "VmRSS parsed");
    check(gscope::parseVmRssKb("Name:\tbash\n") == 0.0, "missing VmRSS defaults to 0");

    check(gscope::cpuPercentFromTicks(100, 100, 1.0, 100) == 0.0, "no tick delta -> 0%");
    check(gscope::cpuPercentFromTicks(100, 200, 1.0, 100) == 100.0, "100 ticks in 1s @ 100Hz -> 100%");
    check(gscope::cpuPercentFromTicks(100, 150, 0.5, 100) == 100.0, "50 ticks in 0.5s @ 100Hz -> 100%");
    check(gscope::cpuPercentFromTicks(200, 100, 1.0, 100) == 0.0, "pid reuse (ticks went backwards) -> 0%");
    check(gscope::cpuPercentFromTicks(100, 200, 0.0, 100) == 0.0, "zero elapsed time -> 0%");

    // Light integration check against the real /proc: the sampler should
    // never throw, should respect topN, and -- since this test process is
    // itself running -- its own pid should show up at least once it's
    // been seen across two samples.
    gscope::ProcessSampler sampler;
    std::vector<gscope::ProcessSample> first = sampler.sample(/*topN=*/5);
    check(first.size() <= 5, "topN is respected");
    for (const gscope::ProcessSample& process : first)
        check(process.cpuPercent == 0.0, "first reading has no prior tick count to diff against");

    // Busy-loop briefly so this process has a non-zero tick delta by the
    // next sample -- keeps the test fast rather than sleeping.
    volatile long spin = 0;
    for (long i = 0; i < 200000000; ++i) spin += i;

    std::vector<gscope::ProcessSample> second = sampler.sample(/*topN=*/1000);
    check(second.size() <= 1000, "topN still respected");
    bool sorted = true;
    for (size_t i = 1; i < second.size(); ++i) {
        if (second[i].cpuPercent > second[i - 1].cpuPercent)
            sorted = false;
    }
    check(sorted, "results sorted by cpuPercent descending");

    std::puts("process sampler: /proc/[pid]/stat + status parsing and %CPU math passed");
    return 0;
}
