// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/metrics_service.hpp"

#include "gscope/frontend_version.hpp"
#include "gscope/json_codec.hpp"

namespace gscope {

MetricsService::MetricsService(std::unique_ptr<MetricsProvider> provider, size_t historyCapacity,
                               std::string publicDir, std::chrono::seconds buildIdCheckInterval)
    : provider_(std::move(provider)), history_(historyCapacity), publicDir_(std::move(publicDir)),
      buildIdCheckInterval_(buildIdCheckInterval)
{}

MetricsService::~MetricsService()
{
    stop();
}

bool MetricsService::start()
{
    return provider_->start([this](const Sample& sample) { onSample(sample); });
}

void MetricsService::stop()
{
    provider_->stop();
}

std::string MetricsService::latestJson() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latestJson_;
}

std::string MetricsService::historyJson() const
{
    std::vector<std::string> entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries = history_.snapshot();
    }
    std::string out = "[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i != 0)
            out += ',';
        out += entries[i];
    }
    out += ']';
    return out;
}

void MetricsService::addSubscriber(const std::shared_ptr<http::WebSocketConnection>& connection)
{
    const std::string buildId = currentBuildId();
    std::string backlog;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.push_back(connection);
        std::vector<std::string> entries = history_.snapshot();
        backlog = "{\"type\":\"history\",\"samples\":[";
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i != 0)
                backlog += ',';
            backlog += entries[i];
        }
        backlog += "],\"buildId\":\"" + buildId + "\"}";
    }
    connection->send(backlog);
}

void MetricsService::onSample(const Sample& sample)
{
    Sample enriched = sample;
    enriched.processes = processSampler_.sample();
    enriched.network = networkSampler_.sample();

    std::string encoded = sampleToJson(enriched).dump();
    const std::string buildId = currentBuildId();
    std::string message =
        "{\"type\":\"sample\",\"sample\":" + encoded + ",\"buildId\":\"" + buildId + "\"}";
    auto payload = std::make_shared<const std::string>(std::move(message));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        latestJson_ = encoded;
        history_.push(std::move(encoded));
    }

    broadcast(payload);
}

std::string MetricsService::currentBuildId()
{
    if (publicDir_.empty())
        return {};

    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (haveBuildId_ && (now - lastBuildIdCheck_) < buildIdCheckInterval_)
        return cachedBuildId_;

    cachedBuildId_ = computeDirectoryFingerprint(publicDir_);
    lastBuildIdCheck_ = now;
    haveBuildId_ = true;
    return cachedBuildId_;
}

void MetricsService::broadcast(const std::shared_ptr<const std::string>& payload)
{
    std::vector<std::weak_ptr<http::WebSocketConnection>> subscribers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers = subscribers_;
    }

    std::vector<std::weak_ptr<http::WebSocketConnection>> alive;
    alive.reserve(subscribers.size());
    for (auto& weak : subscribers) {
        std::shared_ptr<http::WebSocketConnection> connection = weak.lock();
        if (!connection)
            continue;
        alive.push_back(weak);
        connection->post([connection, payload] { connection->send(*payload); });
    }

    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_ = std::move(alive);
}

}  // namespace gscope
