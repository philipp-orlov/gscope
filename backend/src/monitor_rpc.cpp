// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/monitor_rpc.hpp"

namespace gscope {

void MonitorRpc::Watch(rpc::ServerContext& context, rpc::ServerWriter<wire::Update>& writer)
{
    MetricsService::Subscription subscription = metrics_.watch(queueCapacity_);
    const std::shared_ptr<Watcher> watcher = subscription.watcher;
    // The client cancelled, its connection closed or the endpoint is shutting
    // down: wake the wait in next(). The hook only takes the watcher's lock.
    context.onCancel([watcher] { watcher->close(); });

    bool open = true;
    for (const bufs::Message& update : subscription.backlog)
        if (!(open = writer.write(update)))
            break;
    while (open) {
        UpdateMessage update = watcher->next();
        open = update && writer.write(*update);
    }

    context.onCancel({});
    metrics_.unwatch(watcher);
    if (watcher->overflowed())
        throw rpc::Error(rpc::Code::ResourceExhausted, "client fell behind; reconnect for a fresh backlog");
}

}  // namespace gscope
