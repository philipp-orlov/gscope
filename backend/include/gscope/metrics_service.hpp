// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: glues a MetricsProvider to a bounded history and a set of
// watchers. One background callback in (from the provider's own thread);
// each sample is encoded once (wire_codec.hpp) and the same message is
// queued for every watcher. Knows nothing of the transport: MonitorRpc
// (monitor_rpc.hpp) drains a watcher into a librpc stream.
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "bufs/bufs.hpp"
#include "gscope/history_buffer.hpp"
#include "gscope/host_info.hpp"
#include "gscope/metrics.hpp"
#include "gscope/network_sampler.hpp"
#include "gscope/process_sampler.hpp"
#include "gscope/provider.hpp"
#include "gscope/wire_codec.hpp"

namespace gscope {

using UpdateMessage = std::shared_ptr<const bufs::Message>;

// One client's queue of encoded updates.
class Watcher
{
public:
    explicit Watcher(size_t capacity) : capacity_(capacity) {}

    // The next update, waiting for it; null once the watcher is closed
    // (the service stopped, the client went away, or it fell behind).
    UpdateMessage next();
    // Wakes next() for good. Safe from any thread, more than once.
    void close();
    // True when it was closed because `capacity` updates were waiting.
    bool overflowed() const;

private:
    friend class MetricsService;
    void offer(const UpdateMessage& update);

    const size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<UpdateMessage> queue_;
    bool closed_ = false;
    bool overflowed_ = false;
};

class MetricsService
{
public:
    // publicDir, when non-empty, is fingerprinted (see frontend_version.hpp)
    // into the Inventory's build_id, so a connected page can detect a
    // redeployed frontend and reload itself. buildIdCheckInterval bounds how
    // often the directory is re-scanned; tests pass 0 to see every change.
    explicit MetricsService(std::unique_ptr<MetricsProvider> provider, size_t historyCapacity = 120,
                            std::string publicDir = {},
                            std::chrono::seconds buildIdCheckInterval = std::chrono::seconds(5),
                            HostInfo host = HostInfo::local());
    ~MetricsService();

    // Starts the provider. Returns false if the provider failed to start
    // (e.g. its binary is missing).
    bool start();
    // Stops the provider and closes every watcher.
    void stop();

    const char* providerName() const { return provider_->name(); }

    // Samples per backlog update: a long --history-size must not exceed the
    // largest message librpc carries (16 MiB); 512 samples are ~300 KB.
    static constexpr size_t kBacklogChunk = 512;

    struct Subscription
    {
        std::shared_ptr<Watcher> watcher;
        // The updates to send first: the inventory with the first chunk of the
        // backlog, then the rest of the backlog. At least one.
        std::vector<bufs::Message> backlog;
    };
    // A new watcher. Its first update and its queue meet exactly: no sample
    // is in both, none is in neither.
    Subscription watch(size_t queueCapacity = 64);
    void unwatch(const std::shared_ptr<Watcher>& watcher);
    size_t watcherCount() const;

    // What a new watcher would see; for tests and diagnostics.
    std::vector<Sample> history() const;
    Inventory inventory() const;

private:
    void onSample(const Sample& sample);
    std::string currentBuildId();

    std::unique_ptr<MetricsProvider> provider_;
    HostInfo host_;
    // Process listing and network throughput are /proc-based and
    // provider-independent, so every provider's Sample gets them here.
    ProcessSampler processSampler_;
    NetworkSampler networkSampler_;

    std::string publicDir_;
    std::chrono::seconds buildIdCheckInterval_;
    std::mutex buildIdMutex_;
    std::string cachedBuildId_;
    std::chrono::steady_clock::time_point lastBuildIdCheck_{};
    bool haveBuildId_ = false;

    mutable std::mutex mutex_;
    HistoryBuffer<Sample> history_;
    Inventory inventory_;
    std::vector<std::shared_ptr<Watcher>> watchers_;
    bool stopped_ = false;
};

}  // namespace gscope
