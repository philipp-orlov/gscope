// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/metrics_service.hpp"

#include <filesystem>
#include <fstream>
#include <thread>

#include "fake_provider.hpp"

using gscope_test::check;
using gscope_test::FakeProvider;
using gscope_test::makeSample;

namespace {

gscope::wire::Update::Reader read(const bufs::Message& message)
{
    check(bufs::verify<gscope::wire::Update>(message.data(), message.size()), "every update verifies");
    return bufs::root<gscope::wire::Update>(message.data());
}

uint16_t firstCore(const gscope::wire::Sample::Reader& sample)
{
    return sample.cores()[0].utilizationCenti;
}

}  // namespace

int main()
{
    auto providerOwner = std::make_unique<FakeProvider>();
    FakeProvider* provider = providerOwner.get();
    gscope::MetricsService service(std::move(providerOwner), /*historyCapacity=*/2, {}, std::chrono::seconds(5),
                                   gscope_test::testHost());
    check(service.start(), "service starts");
    check(std::string(service.providerName()) == "fake", "provider name forwarded");

    // Before any sample: one update, the inventory and no samples.
    {
        auto subscription = service.watch();
        check(subscription.backlog.size() == 1, "one first update");
        auto first = read(subscription.backlog[0]);
        check(first.inventory() && first.inventory().hostname() == "test-host" && first.inventory().provider() == "fake",
              "the first update says who it talks to");
        check(first.samples().size() == 0, "no backlog yet");
        service.unwatch(subscription.watcher);
    }

    auto subscription = service.watch();
    std::shared_ptr<gscope::Watcher> watcher = subscription.watcher;
    check(service.watcherCount() == 1, "registered");

    provider->emit(makeSample(10.0));
    gscope::UpdateMessage live = watcher->next();
    check(live != nullptr, "a sample is queued for the watcher");
    auto update = read(*live);
    check(update.inventory() && update.inventory().gpus().size() == 1, "the first sample brings the GPUs: inventory");
    check(update.samples().size() == 1 && firstCore(update.samples()[0]) == 1000, "the reading");

    provider->emit(makeSample(20.0));
    live = watcher->next();
    check(!read(*live).inventory(), "nothing changed: no inventory, numbers only");

    provider->emit(makeSample(30.0));  // historyCapacity=2: the first reading falls out
    live = watcher->next();
    check(firstCore(read(*live).samples()[0]) == 3000, "in order");

    {
        auto late = service.watch();
        auto first = read(late.backlog[0]);
        check(first.inventory() && first.inventory().gpus()[0].name() == "NVIDIA GB10", "a late watcher gets the inventory");
        check(first.samples().size() == 2 && firstCore(first.samples()[0]) == 2000 && firstCore(first.samples()[1]) == 3000,
              "and the backlog, oldest first, without the evicted sample");
        service.unwatch(late.watcher);
    }

    // The same encoded message goes to every watcher.
    {
        auto other = service.watch();
        provider->emit(makeSample(40.0));
        check(watcher->next().get() == other.watcher->next().get(), "one encoding shared by all watchers");
        service.unwatch(other.watcher);
    }

    // A watcher that falls behind is closed, not grown without bound.
    {
        auto slow = service.watch(/*queueCapacity=*/2);
        for (int i = 0; i < 3; ++i)
            provider->emit(makeSample(i));
        check(slow.watcher->overflowed(), "overflowed");
        check(slow.watcher->next() == nullptr, "and closed");
        check(service.watcherCount() == 1, "and dropped from the service");
    }

    // stop() closes every watcher, waking a waiting next().
    std::thread waiter([&] {
        // drain what is queued, then wait
        while (watcher->next()) {
        }
    });
    service.stop();
    waiter.join();
    check(service.watcherCount() == 0, "stop drops the watchers");

    // A long history goes out in chunks, the inventory with the first.
    {
        auto owner = std::make_unique<FakeProvider>();
        FakeProvider* p = owner.get();
        gscope::MetricsService big(std::move(owner), 1200, {}, std::chrono::seconds(5), gscope_test::testHost());
        big.start();
        for (int i = 0; i < 1200; ++i)
            p->emit(makeSample(i % 100));
        auto s = big.watch();
        check(s.backlog.size() == 3, "1200 samples in chunks of 512");
        check(read(s.backlog[0]).inventory() && !read(s.backlog[1]).inventory(), "the inventory once, first");
        size_t total = 0;
        for (const auto& m : s.backlog)
            total += read(m).samples().size();
        check(total == 1200, "every sample once");
        big.stop();
    }

    // build_id: a fingerprint of publicDir, and a redeployed frontend changes
    // it, which puts the inventory into the next update.
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "gscope_metrics_service_test";
        fs::remove_all(dir);
        fs::create_directories(dir);
        { std::ofstream(dir / "index.html") << "v1"; }

        auto owner = std::make_unique<FakeProvider>();
        FakeProvider* p = owner.get();
        gscope::MetricsService versioned(std::move(owner), 2, dir.string(), std::chrono::seconds(0),
                                         gscope_test::testHost());
        versioned.start();
        auto s = versioned.watch();
        const std::string firstBuildId(read(s.backlog[0]).inventory().buildId());
        check(!firstBuildId.empty() && firstBuildId != "0", "build_id reflects the directory");

        p->emit(makeSample(5.0));
        auto first = read(*s.watcher->next());
        check(first.inventory() && first.inventory().buildId() == firstBuildId, "unchanged deploy: same build_id");
        p->emit(makeSample(5.0));
        check(!read(*s.watcher->next()).inventory(), "and no inventory while nothing changes");

        { std::ofstream(dir / "index.html") << "v2 - different length entirely"; }
        p->emit(makeSample(6.0));
        auto changed = read(*s.watcher->next());
        check(changed.inventory() && changed.inventory().buildId() != firstBuildId,
              "redeployed frontend: the inventory comes again with the new build_id");
        versioned.stop();
        fs::remove_all(dir);
    }

    std::puts("metrics service: backlog, inventory on change, shared encoding, overflow, stop, chunks and build_id passed");
    return 0;
}
