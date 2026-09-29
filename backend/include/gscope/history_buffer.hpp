// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: fixed-capacity ring buffer of the most recent samples, kept as
// already-serialized JSON strings so broadcasting or answering a "give me
// the history" REST/WS request never re-encodes a sample twice.
#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace gscope {

class HistoryBuffer
{
public:
    explicit HistoryBuffer(size_t capacity) : capacity_(capacity) {}

    void push(std::string encodedSample)
    {
        if (capacity_ == 0)
            return;
        if (entries_.size() >= capacity_)
            entries_.pop_front();
        entries_.push_back(std::move(encodedSample));
    }

    // Oldest first -- the order a chart replays them in.
    std::vector<std::string> snapshot() const
    {
        return std::vector<std::string>(entries_.begin(), entries_.end());
    }

    size_t size() const { return entries_.size(); }
    size_t capacity() const { return capacity_; }
    bool empty() const { return entries_.empty(); }

private:
    size_t capacity_;
    std::deque<std::string> entries_;
};

}  // namespace gscope
