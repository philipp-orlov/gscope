// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/history_buffer.hpp"

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
    gscope::HistoryBuffer buffer(3);
    check(buffer.empty(), "starts empty");

    buffer.push("a");
    buffer.push("b");
    buffer.push("c");
    check(buffer.size() == 3, "holds up to capacity");
    {
        std::vector<std::string> snapshot = buffer.snapshot();
        check((snapshot == std::vector<std::string>{"a", "b", "c"}), "oldest first");
    }

    buffer.push("d");  // evicts "a"
    check(buffer.size() == 3, "stays at capacity");
    {
        std::vector<std::string> snapshot = buffer.snapshot();
        check((snapshot == std::vector<std::string>{"b", "c", "d"}), "oldest evicted first");
    }

    gscope::HistoryBuffer zeroCapacity(0);
    zeroCapacity.push("x");
    check(zeroCapacity.empty(), "zero-capacity buffer never holds anything");

    std::puts("history buffer: capacity and eviction order passed");
    return 0;
}
