// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/metrics_service.hpp"

#include <algorithm>

#include "gscope/frontend_version.hpp"

namespace gscope {

UpdateMessage Watcher::next()
{
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this] { return closed_ || !queue_.empty(); });
    if (closed_)
        return nullptr;
    UpdateMessage update = std::move(queue_.front());
    queue_.pop_front();
    return update;
}

void Watcher::close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        queue_.clear();
    }
    ready_.notify_all();
}

bool Watcher::overflowed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return overflowed_;
}

void Watcher::offer(const UpdateMessage& update)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_)
            return;
        // A client this far behind would show stale numbers for a long
        // time; ending it lets it reconnect and start from a fresh backlog.
        if (queue_.size() >= capacity_) {
            overflowed_ = true;
            closed_ = true;
            queue_.clear();
        } else {
            queue_.push_back(update);
        }
    }
    ready_.notify_all();
}

MetricsService::MetricsService(std::unique_ptr<MetricsProvider> provider, size_t historyCapacity,
                               std::string publicDir, std::chrono::seconds buildIdCheckInterval, HostInfo host,
                               std::string fixedBuildId)
    : provider_(std::move(provider)), host_(std::move(host)), publicDir_(std::move(publicDir)),
      fixedBuildId_(std::move(fixedBuildId)),
      buildIdCheckInterval_(buildIdCheckInterval), history_(historyCapacity)
{
    // Before the first sample a watcher still learns who it talks to.
    inventory_.host = host_;
    inventory_.provider = provider_->name();
    inventory_.buildId = currentBuildId();
}

MetricsService::~MetricsService()
{
    stop();
}

bool MetricsService::start()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = false;
    }
    return provider_->start([this](const Sample& sample) { onSample(sample); });
}

void MetricsService::stop()
{
    provider_->stop();
    std::vector<std::shared_ptr<Watcher>> watchers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        watchers.swap(watchers_);
    }
    for (auto& watcher : watchers)
        watcher->close();
}

MetricsService::Subscription MetricsService::watch(size_t queueCapacity)
{
    auto watcher = std::make_shared<Watcher>(queueCapacity);
    std::lock_guard<std::mutex> lock(mutex_);
    const std::vector<Sample> samples = history_.snapshot();
    std::vector<bufs::Message> backlog;
    size_t at = 0;
    do {
        const size_t count = std::min(kBacklogChunk, samples.size() - at);
        backlog.push_back(encodeUpdate(at == 0 ? &inventory_ : nullptr, inventory_, samples.data() + at, count));
        at += count;
    } while (at < samples.size());
    if (stopped_)
        watcher->close();
    else
        watchers_.push_back(watcher);
    return {std::move(watcher), std::move(backlog)};
}

void MetricsService::unwatch(const std::shared_ptr<Watcher>& watcher)
{
    std::lock_guard<std::mutex> lock(mutex_);
    watchers_.erase(std::remove(watchers_.begin(), watchers_.end(), watcher), watchers_.end());
}

size_t MetricsService::watcherCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return watchers_.size();
}

std::vector<Sample> MetricsService::history() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return history_.snapshot();
}

Inventory MetricsService::inventory() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return inventory_;
}

void MetricsService::onSample(const Sample& sample)
{
    Sample enriched = sample;
    const auto now = std::chrono::steady_clock::now();
    if (watcherCount() == 0 && lastScan_ != std::chrono::steady_clock::time_point{} &&
        now - lastScan_ < kIdleScanInterval) {
        enriched.processes = lastProcesses_;
        enriched.network = lastNetwork_;
    } else {
        enriched.processes = processSampler_.sample();
        enriched.network = networkSampler_.sample();
        lastProcesses_ = enriched.processes;
        lastNetwork_ = enriched.network;
        lastScan_ = now;
    }
    Inventory inventory = inventoryOf(enriched, host_, provider_->name(), currentBuildId());

    // Under the lock from encoding to queueing, so that watch() sees each
    // sample either in its backlog or in its queue, never both.
    std::lock_guard<std::mutex> lock(mutex_);
    const bool changed = inventory != inventory_;
    if (changed)
        inventory_ = std::move(inventory);
    history_.push(enriched);
    auto update = std::make_shared<const bufs::Message>(
        encodeUpdate(changed ? &inventory_ : nullptr, inventory_, &enriched, 1));
    for (auto& watcher : watchers_)
        watcher->offer(update);
    // A watcher that fell behind is closed; its handler unwatches it, but
    // there is no need to keep queueing for it until then.
    watchers_.erase(std::remove_if(watchers_.begin(), watchers_.end(),
                                   [](const std::shared_ptr<Watcher>& w) { return w->overflowed(); }),
                    watchers_.end());
}

std::string MetricsService::currentBuildId()
{
    if (!fixedBuildId_.empty())
        return fixedBuildId_;
    if (publicDir_.empty())
        return {};

    std::lock_guard<std::mutex> lock(buildIdMutex_);
    const auto now = std::chrono::steady_clock::now();
    if (haveBuildId_ && (now - lastBuildIdCheck_) < buildIdCheckInterval_)
        return cachedBuildId_;

    cachedBuildId_ = computeDirectoryFingerprint(publicDir_);
    lastBuildIdCheck_ = now;
    haveBuildId_ = true;
    return cachedBuildId_;
}

}  // namespace gscope
