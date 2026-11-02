// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "gscope/line_stream.hpp"

namespace {
void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}
using Clock = std::chrono::steady_clock;
}  // namespace

int main()
{
    // The argv form runs no shell: metacharacters are plain text, and a missing program is just an empty stream.
    {
        gscope::PopenLineStream stream(std::vector<std::string>{"/bin/echo", "$(touch /tmp/gscope_never); a|b"});
        std::string line;
        check(stream.nextLine(line) && line == "$(touch /tmp/gscope_never); a|b", "argv form is not interpreted");
        gscope::PopenLineStream missing(std::vector<std::string>{"/definitely/not/here"});
        check(!missing.nextLine(line), "missing program gives EOF");
        check(access("/tmp/gscope_never", F_OK) != 0, "no shell ran");
    }

    // Lines are split, a last line without a newline is delivered, then the stream ends.
    {
        gscope::PopenLineStream stream("printf 'one\\ntwo\\nthree'");
        std::string line;
        check(stream.nextLine(line) && line == "one", "first line");
        check(stream.nextLine(line) && line == "two", "second line");
        check(stream.nextLine(line) && line == "three", "last line without newline");
        check(!stream.nextLine(line), "end of stream");
    }

    // cancel() from another thread wakes a reader blocked on a silent child, promptly, and destroying the stream
    // afterwards stops the child (it would otherwise run for a minute).
    {
        gscope::PopenLineStream stream("echo first; sleep 60");
        std::string line;
        check(stream.nextLine(line) && line == "first", "line before the silence");
        const auto start = Clock::now();
        std::atomic<bool> returned{false};
        std::thread reader([&] {
            std::string unused;
            check(!stream.nextLine(unused), "blocked read returns false when cancelled");
            returned = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        check(!returned, "the reader is blocked while the child is silent");
        stream.cancel();
        reader.join();
        check(Clock::now() - start < std::chrono::seconds(2), "cancel wakes the reader promptly");
    }
    check(Clock::now().time_since_epoch().count() > 0, "clock");

    // Every child was reaped: there is nothing left to wait for.
    check(waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD, "no child left behind");

    // A command that does not exist ends the stream instead of hanging.
    {
        gscope::PopenLineStream stream("exec /nonexistent/binary 2>/dev/null");
        std::string line;
        check(!stream.nextLine(line), "failed command ends the stream");
    }

    std::puts("line stream: lines, cancel, reaping passed");
}
