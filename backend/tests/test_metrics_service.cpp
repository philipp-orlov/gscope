// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/metrics_service.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

// A provider that only ever emits samples handed to it explicitly by the
// test, synchronously on the calling thread -- no subprocess, no timing.
class FakeProvider : public gscope::MetricsProvider
{
public:
    const char* name() const override { return "fake"; }
    bool start(SampleCallback callback) override
    {
        callback_ = std::move(callback);
        return true;
    }
    void stop() override {}
    void emit(const gscope::Sample& sample) { callback_(sample); }

private:
    SampleCallback callback_;
};

gscope::Sample makeSample(double cpuUtilization)
{
    gscope::Sample sample;
    sample.timestamp = std::chrono::system_clock::now();
    sample.cpuCores.push_back({cpuUtilization, 1500.0});
    sample.memUsedMb = 1000;
    sample.memTotalMb = 8000;
    return sample;
}
}  // namespace

int main()
{
    auto providerOwner = std::make_unique<FakeProvider>();
    FakeProvider* provider = providerOwner.get();

    gscope::MetricsService service(std::move(providerOwner), /*historyCapacity=*/2);
    check(service.start(), "service starts");
    check(std::strcmp(service.providerName(), "fake") == 0, "provider name forwarded");
    check(service.latestJson() == "{}", "latest is empty before any sample");
    check(service.historyJson() == "[]", "history is empty before any sample");

    auto connection = std::make_shared<http::WebSocketConnection>();
    std::vector<std::string> sentMessages;
    connection->bindSendFunction([&](std::string_view data, bool /*binary*/) {
        sentMessages.emplace_back(data);
    });
    connection->bindPostFunction([](std::function<void()> task) {
        task();  // no real IoLoop in this test; run inline instead
        return true;
    });

    service.addSubscriber(connection);
    check(sentMessages.size() == 1, "subscriber gets an immediate history message");
    check(contains(sentMessages[0], "\"type\":\"history\""), "backlog message is tagged");
    check(contains(sentMessages[0], "\"samples\":[]"), "backlog is empty so far");

    provider->emit(makeSample(10.0));
    check(sentMessages.size() == 2, "subscriber gets a sample message after emit");
    check(contains(sentMessages[1], "\"type\":\"sample\""), "live message is tagged");
    check(contains(sentMessages[1], "\"utilization\":10"), "sample carries the CPU reading");
    check(contains(service.latestJson(), "\"utilization\":10"), "latestJson reflects the reading");

    provider->emit(makeSample(20.0));
    provider->emit(makeSample(30.0));  // historyCapacity=2, so the first reading falls out
    check(sentMessages.size() == 4, "one broadcast per sample");

    std::string history = service.historyJson();
    check(!contains(history, "\"utilization\":10"), "oldest sample evicted from history");
    check(contains(history, "\"utilization\":20"), "second sample kept");
    check(contains(history, "\"utilization\":30"), "third sample kept");

    service.stop();

    // buildId: reports a fingerprint of publicDir, and it changes when the
    // deployed frontend build changes underneath a running server -- the
    // signal a connected client uses to know it should reload.
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "gscope_metrics_service_test";
        fs::remove_all(dir);
        fs::create_directories(dir);
        { std::ofstream(dir / "index.html") << "v1"; }

        auto versionedProviderOwner = std::make_unique<FakeProvider>();
        FakeProvider* versionedProvider = versionedProviderOwner.get();
        gscope::MetricsService versioned(std::move(versionedProviderOwner), /*historyCapacity=*/2,
                                       dir.string(), std::chrono::seconds(0));
        check(versioned.start(), "versioned service starts");

        auto connection2 = std::make_shared<http::WebSocketConnection>();
        std::vector<std::string> messages2;
        connection2->bindSendFunction(
            [&](std::string_view data, bool /*binary*/) { messages2.emplace_back(data); });
        connection2->bindPostFunction([](std::function<void()> task) {
            task();
            return true;
        });

        versioned.addSubscriber(connection2);
        check(contains(messages2[0], "\"buildId\":\""), "history message carries a buildId");
        size_t marker = messages2[0].find("\"buildId\":\"") + 11;
        std::string firstBuildId = messages2[0].substr(marker, messages2[0].find('"', marker) - marker);
        check(!firstBuildId.empty() && firstBuildId != "0", "buildId reflects the temp dir contents");

        versionedProvider->emit(makeSample(5.0));
        check(messages2.back().find("\"buildId\":\"" + firstBuildId + "\"") != std::string::npos,
              "unchanged deploy -> same buildId");

        { std::ofstream(dir / "index.html") << "v2 - different length entirely"; }
        versionedProvider->emit(makeSample(6.0));
        check(messages2.back().find("\"buildId\":\"" + firstBuildId + "\"") == std::string::npos,
              "redeployed frontend -> different buildId");

        versioned.stop();
        fs::remove_all(dir);
    }

    std::puts("metrics service: history, latest, subscriber broadcast and buildId passed");
    return 0;
}
