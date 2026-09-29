// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: a source of text lines. The real implementation runs a
// subprocess (tegrastats, nvidia-smi) and reads its stdout a line at a
// time; VectorLineStream replays canned lines instead, which is what lets
// the providers below be unit-tested without the real binaries.
#pragma once

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gscope {

class LineStream
{
public:
    virtual ~LineStream() = default;
    // Blocks until a line is available. Returns false at EOF/error, in
    // which case the provider's read loop exits.
    virtual bool nextLine(std::string& out) = 0;
    // Best-effort: unblocks a nextLine() call that's parked waiting for
    // input, without destroying *this. A provider's stop() must call this
    // (from the thread stopping the provider) and join() the reader thread
    // before actually destroying the stream -- destroying it first, while
    // the reader thread might still be inside nextLine(), is a
    // heap-use-after-free race. Default no-op: streams that never block
    // (e.g. VectorLineStream) don't need it.
    virtual void cancel() {}
};

// Runs `command` via popen(..., "r") and yields its stdout one line at a
// time. cancel() closes the underlying fd to unblock a thread parked in
// fgets(); the destructor (called only after that thread has been joined)
// then pclose()s to reap the child.
class PopenLineStream : public LineStream
{
public:
    explicit PopenLineStream(const std::string& command);
    ~PopenLineStream() override;

    bool nextLine(std::string& out) override;
    void cancel() override;

private:
    FILE* pipe_ = nullptr;
};

// Test double: replays a fixed list of lines, then reports EOF.
class VectorLineStream : public LineStream
{
public:
    explicit VectorLineStream(std::vector<std::string> lines) : lines_(std::move(lines)) {}

    bool nextLine(std::string& out) override
    {
        if (index_ >= lines_.size())
            return false;
        out = lines_[index_++];
        return true;
    }

private:
    std::vector<std::string> lines_;
    size_t index_ = 0;
};

using LineStreamFactory = std::function<std::unique_ptr<LineStream>()>;

}  // namespace gscope
