// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope: the Monitor service of monitor.bufs over librpc. Each Watch call
// is one MetricsService watcher: the backlog (the first update also carries the inventory),
// then the shared, already encoded live updates as they come.
#pragma once

#include <cstddef>

#include "gscope/metrics_service.hpp"
#include "monitor.rpc.hpp"

namespace gscope {

class MonitorRpc : public wire::MonitorService
{
public:
    // queueCapacity: updates a slow client may fall behind before its call is
    // ended with ResourceExhausted (it reconnects with a fresh backlog).
    explicit MonitorRpc(MetricsService& metrics, size_t queueCapacity = 64)
        : metrics_(metrics), queueCapacity_(queueCapacity)
    {}

    void Watch(rpc::ServerContext& context, rpc::ServerWriter<wire::Update>& writer) override;

private:
    MetricsService& metrics_;
    size_t queueCapacity_;
};

}  // namespace gscope
