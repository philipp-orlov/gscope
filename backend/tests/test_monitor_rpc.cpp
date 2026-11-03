// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Monitor.Watch end to end: an http::Server with the Monitor service on
// /rpc, as gscope-daemon has it, and a librpc client over a real WebSocket.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>

#include "fake_provider.hpp"
#include "gscope/monitor_rpc.hpp"
#include "http/server.hpp"
#include "rpc/client.hpp"
#include "rpc/server.hpp"

using gscope_test::check;
using namespace std::chrono_literals;

namespace {

uint16_t freePort()
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    socklen_t len = sizeof addr;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    const uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

}  // namespace

int main()
{
    auto owner = std::make_unique<gscope_test::FakeProvider>();
    gscope_test::FakeProvider* provider = owner.get();
    gscope::MetricsService metrics(std::move(owner), 10, {}, std::chrono::seconds(5), gscope_test::testHost());
    check(metrics.start(), "service starts");
    provider->emit(gscope_test::makeSample(10.0));

    http::ServerConfig config;
    config.host = "127.0.0.1";
    config.port = freePort();
    config.ioThreads = 1;
    http::Server server(config);
    gscope::MonitorRpc monitor(metrics, /*queueCapacity=*/8);
    rpc::Endpoint endpoint(rpc::EndpointOptions().workers(4));
    endpoint.add(monitor).attach(server, "/rpc");

    std::atomic<bool> listening{false}, stop{false};
    std::thread serving([&] { server.run([&] { return stop.load(); }, [&] { listening = true; }); });
    for (int i = 0; i < 200 && !listening; ++i)
        std::this_thread::sleep_for(10ms);
    check(listening, "server listens");

    {
        rpc::Client connection = rpc::Client::connect("ws://127.0.0.1:" + std::to_string(config.port) + "/rpc");
        gscope::wire::MonitorClient client(connection);
        auto stream = client.Watch();

        check(stream.next(), "the first update arrives");
        check(stream->inventory() && stream->inventory().hostname() == "test-host", "with the inventory");
        check(stream->samples().size() == 1 && stream->samples()[0].cores()[0].utilizationCenti == 1000,
              "and the backlog");

        // A live sample, while the call is open.
        while (metrics.watcherCount() == 0)
            std::this_thread::sleep_for(5ms);
        provider->emit(gscope_test::makeSample(55.5));
        check(stream.next(), "a live update arrives");
        check(!stream->inventory(), "numbers only");
        check(stream->samples()[0].cores()[0].utilizationCenti == 5550, "the new reading");
        // Leaving the scope cancels the stream; the handler unwatches.
    }
    for (int i = 0; i < 200 && metrics.watcherCount() != 0; ++i)
        std::this_thread::sleep_for(10ms);
    check(metrics.watcherCount() == 0, "a cancelled stream releases its watcher");

    // Stopping the service ends an open stream at once.
    {
        rpc::Client connection = rpc::Client::connect("ws://127.0.0.1:" + std::to_string(config.port) + "/rpc");
        gscope::wire::MonitorClient client(connection);
        auto stream = client.Watch();
        check(stream.next(), "first update");
        std::thread stopper([&] {
            std::this_thread::sleep_for(50ms);
            metrics.stop();
        });
        check(!stream.next(), "the stream ends when the service stops");
        stopper.join();
    }

    endpoint.shutdown(1s);
    stop = true;
    serving.join();
    std::puts("monitor rpc: watch over a WebSocket, live update, cancel and stop passed");
    return 0;
}
