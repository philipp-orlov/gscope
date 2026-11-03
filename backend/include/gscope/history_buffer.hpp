// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: fixed-capacity ring buffer of the most recent samples, the
// backlog a new watcher receives (MetricsService keeps hw::Sample values
// and encodes the backlog on demand).
#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace gscope {

template <class T>
class HistoryBuffer
{
public:
    explicit HistoryBuffer(size_t capacity) : capacity_(capacity) {}

    void push(T entry)
    {
        if (capacity_ == 0)
            return;
        if (entries_.size() >= capacity_)
            entries_.pop_front();
        entries_.push_back(std::move(entry));
    }

    // Oldest first -- the order a chart replays them in.
    std::vector<T> snapshot() const { return std::vector<T>(entries_.begin(), entries_.end()); }

    size_t size() const { return entries_.size(); }
    size_t capacity() const { return capacity_; }
    bool empty() const { return entries_.empty(); }

private:
    size_t capacity_;
    std::deque<T> entries_;
};

}  // namespace gscope
