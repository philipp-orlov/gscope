// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/line_stream.hpp"

#include <unistd.h>

namespace gscope {

PopenLineStream::PopenLineStream(const std::string& command)
{
    pipe_ = popen(command.c_str(), "r");
}

PopenLineStream::~PopenLineStream()
{
    if (pipe_)
        pclose(pipe_);
}

void PopenLineStream::cancel()
{
    // Closes the fd out from under a concurrent, possibly-blocked fgets()
    // in the reader thread, making it return EOF/error. pclose() (which
    // also reaps the child) still runs later from the destructor, once
    // that thread has been joined -- a redundant close() of the same fd
    // number by then is harmless.
    if (pipe_)
        ::close(fileno(pipe_));
}

bool PopenLineStream::nextLine(std::string& out)
{
    if (!pipe_)
        return false;

    out.clear();
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe_)) {
        out += buffer;
        if (!out.empty() && out.back() == '\n') {
            out.pop_back();
            return true;
        }
    }
    return !out.empty();  // last partial line before EOF, otherwise real EOF
}

}  // namespace gscope
