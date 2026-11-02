// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/line_stream.hpp"

#include <chrono>
#include <exception>

namespace gscope {

PopenLineStream::PopenLineStream(std::vector<std::string> argv) { start(std::move(argv)); }

PopenLineStream::PopenLineStream(const std::string& command) { start({"/bin/sh", "-c", command}); }

void PopenLineStream::start(std::vector<std::string> argv)
{
    sys::Process::Options options;
    options.argv = std::move(argv);
    try {
        process_ = std::make_unique<sys::Process>(sys::Process::spawn(options));
    } catch (const std::exception&) {
        process_.reset();   // nextLine() reports EOF at once, like an empty stream
    }
}

PopenLineStream::~PopenLineStream() = default;   // ~Process terminates the group and reaps the child

bool PopenLineStream::nextLine(std::string& out)
{
    if (!process_)
        return false;
    return process_->readLine(out) == sys::Process::ReadResult::Line;
}

void PopenLineStream::cancel()
{
    if (process_)
        process_->interrupt();
}

}  // namespace gscope
