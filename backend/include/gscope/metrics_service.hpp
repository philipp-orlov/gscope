// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: glues a MetricsProvider to a bounded history and a set of
// WebSocket subscribers. One background callback in (from the provider's
// own thread), fanned out to N connections each on their own IoLoop.
#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "http/websocket_connection.hpp"
#include "gscope/history_buffer.hpp"
#include "gscope/metrics.hpp"
#include "gscope/network_sampler.hpp"
#include "gscope/process_sampler.hpp"
#include "gscope/provider.hpp"

namespace gscope {

class MetricsService
{
public:
    // publicDir, when non-empty, is fingerprinted (see frontend_version.hpp)
    // and the fingerprint is included as "buildId" in every broadcast so a
    // connected client can detect a redeployed frontend build and reload
    // itself -- see MetricsService.md / frontend/core/metrics.service.ts.
    // buildIdCheckInterval bounds how often the directory is re-scanned
    // (a cached value is reused in between); tests pass 0 to disable the
    // cache and see every change immediately.
    explicit MetricsService(std::unique_ptr<MetricsProvider> provider, size_t historyCapacity = 120,
                            std::string publicDir = {},
                            std::chrono::seconds buildIdCheckInterval = std::chrono::seconds(5));
    ~MetricsService();

    // Starts the provider. Returns false if the provider failed to start
    // (e.g. its binary is missing).
    bool start();
    void stop();

    const char* providerName() const { return provider_->name(); }

    // Latest sample as JSON text, or "{}" before the first reading.
    std::string latestJson() const;

    // All buffered samples, oldest first, as a JSON array.
    std::string historyJson() const;

    // Registers a live WebSocket connection: it immediately receives the
    // buffered history as one "history" message, then a "sample" message
    // per subsequent reading until it closes. Connections are held by
    // weak_ptr and pruned lazily as they close.
    void addSubscriber(const std::shared_ptr<http::WebSocketConnection>& connection);

private:
    void onSample(const Sample& sample);
    void broadcast(const std::shared_ptr<const std::string>& payload);
    std::string currentBuildId();

    std::unique_ptr<MetricsProvider> provider_;
    HistoryBuffer history_;
    // Process listing is orthogonal to which MetricsProvider is in use --
    // always available via /proc, so every provider's Sample gets one
    // attached here rather than each provider sampling it itself.
    ProcessSampler processSampler_;
    // Same reasoning as processSampler_: network throughput is /proc-based
    // and provider-independent.
    NetworkSampler networkSampler_;

    std::string publicDir_;
    std::chrono::seconds buildIdCheckInterval_;
    std::string cachedBuildId_;
    std::chrono::steady_clock::time_point lastBuildIdCheck_{};
    bool haveBuildId_ = false;

    mutable std::mutex mutex_;
    std::string latestJson_ = "{}";
    std::vector<std::weak_ptr<http::WebSocketConnection>> subscribers_;
};

}  // namespace gscope
