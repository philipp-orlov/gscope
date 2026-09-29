// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/network_sampler.hpp"

#include <cstdio>

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
    // A real /proc/net/dev sample: two header lines, then one line per
    // interface. rxBytes is the first field after the interface name,
    // txBytes the 9th.
    const std::string content =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    "
        "packets errs drop fifo colls carrier compressed\n"
        "    lo: 1000000    500    0    0    0     0          0         0  1000000     "
        "500    0    0    0     0       0          0\n"
        "  eth0: 8000000   4000    0    0    0     0          0       10  2000000    "
        "3000    0    0    0     0       0          0\n";

    auto byInterface = gscope::parseProcNetDev(content);
    check(byInterface.size() == 2, "two interfaces parsed");
    check(byInterface.at("lo").rxBytes == 1000000, "lo rxBytes parsed");
    check(byInterface.at("eth0").rxBytes == 8000000, "eth0 rxBytes parsed");
    check(byInterface.at("eth0").txBytes == 2000000, "eth0 txBytes parsed (9th field)");

    gscope::ProcNetDevCounters total = gscope::sumNonLoopback(byInterface);
    check(total.rxBytes == 8000000, "loopback excluded from the rx sum");
    check(total.txBytes == 2000000, "loopback excluded from the tx sum");

    check(gscope::parseProcNetDev("").empty(), "empty content -> no interfaces");
    check(gscope::sumNonLoopback({}).rxBytes == 0, "no interfaces -> zero sum");

    // Light integration check against the real /proc: never throws, first
    // reading is 0 (nothing to diff against yet), and a second reading a
    // little later reports non-negative rates.
    gscope::NetworkSampler sampler;
    gscope::NetworkSample first = sampler.sample();
    check(first.rxBytesPerSec == 0.0 && first.txBytesPerSec == 0.0,
          "first reading has no prior counters to diff against");

    gscope::NetworkSample second = sampler.sample();
    check(second.rxBytesPerSec >= 0.0 && second.txBytesPerSec >= 0.0, "rates are never negative");

    std::puts("network sampler: /proc/net/dev parsing and rate math passed");
    return 0;
}
