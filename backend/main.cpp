// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope backend: a jetson_stats-style system monitor, served over
// HTTP/WebSocket instead of a terminal UI. The metrics travel as librpc
// calls (include/gscope/wire/monitor.bufs) on /rpc; see README.md.
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <pthread.h>
#include <signal.h>

#include "cfg/cfg.hpp"
#include "gscope/metrics_service.hpp"
#include "gscope/monitor_rpc.hpp"
#include "gscope/provider.hpp"
#include "http/logger.hpp"
#include "http/server.hpp"
#include "hw/log.hpp"
#include "rpc/server.hpp"

using namespace http;

#ifdef GSCOPE_EMBEDDED_INDEX_HTML
// Defined by objcopy (see CMakeLists.txt) from public/index.html; not real
// pointers into an array, just symbols marking where the linker placed the
// embedded bytes in the binary's .data section.
extern "C" {
extern const char _binary_index_html_gz_start[];
extern const char _binary_index_html_gz_end[];
}
#include <zlib.h>
#endif

namespace {
#ifdef GSCOPE_EMBEDDED_INDEX_HTML
std::string_view embeddedGzip()
{
    return {_binary_index_html_gz_start,
            static_cast<size_t>(_binary_index_html_gz_end - _binary_index_html_gz_start)};
}

// For the rare client that does not accept gzip (curl without --compressed); inflated once, on first use.
const std::string& embeddedPlain()
{
    static const std::string plain = [] {
        const std::string_view gz = embeddedGzip();
        z_stream stream{};
        if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
            throw std::runtime_error("cannot initialize zlib");
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(gz.data()));
        stream.avail_in = static_cast<uInt>(gz.size());
        std::string out;
        char chunk[64 * 1024];
        int result;
        do {
            stream.next_out = reinterpret_cast<Bytef*>(chunk);
            stream.avail_out = sizeof(chunk);
            result = inflate(&stream, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END) {
                inflateEnd(&stream);
                throw std::runtime_error("the embedded index.html is not valid gzip");
            }
            out.append(chunk, sizeof(chunk) - stream.avail_out);
        } while (result != Z_STREAM_END);
        inflateEnd(&stream);
        return out;
    }();
    return plain;
}
#endif
}  // namespace

int main(int argc, char** argv)
try {
    // Blocked before any thread starts, so every thread inherits the mask and only sigwait() below sees them.
    sigset_t stopSignals;
    sigemptyset(&stopSignals);
    sigaddset(&stopSignals, SIGINT);
    sigaddset(&stopSignals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &stopSignals, nullptr);

    ServerConfig config;
    config.port = 8081;
    // A metrics dashboard serves a handful of WebSocket clients, nowhere
    // near enough load to need one reactor thread per core (the library
    // default) -- that just multiplies idle thread stacks/malloc arenas
    // for no throughput benefit. 2 is plenty; --io-threads overrides it.
    config.ioThreads = 2;
    unsigned intervalMs = 1000;
    unsigned historySize = 120;
    std::string forcedProvider;
    std::string publicDir = "./public";
    bool publicDirOverridden = false;

    cfg::Schema options;
    uint16_t port = 8081;
    std::string host = "0.0.0.0";
    unsigned ioThreads = 2;
    // Each open dashboard holds one endpoint worker for its Watch stream.
    unsigned clients = 4;
    bool help = false;
    options.addInt(port, "port", port, uint16_t{1}, uint16_t{65535}, "listen port");
    options.addString(host, "host", host, "address to listen on; use 127.0.0.1 unless the dashboard must be reachable from other machines");
    options.addInt(intervalMs, "interval-ms", intervalMs, 100u, 3600000u, "sampling interval");
    options.addInt(historySize, "history-size", historySize, 1u, 100000u, "samples kept for new clients");
    options.addString(forcedProvider, "provider", "", "tegrastats|nvml|nvidia-smi|proc (default: detect)");
    options.addString(publicDir, "public-dir", publicDir, "serve the UI from disk instead of the embedded copy");
    options.addInt(ioThreads, "io-threads", ioThreads, 1u, 256u, "reactor threads");
    options.addInt(clients, "clients", clients, 1u, 1024u, "dashboards that can watch at once");
    options.addBool(help, "help", false, "show this text");
    if (auto status = options.fromArgv(argc, argv); !status.ok())
        throw std::invalid_argument(std::string(status.message()));
    if (help) {
        std::printf("gscope-daemon [options]\n%s", options.help().c_str());
        return 0;
    }
    if (!options.positional().empty())
        throw std::invalid_argument("unexpected argument: " + options.positional().front());
    config.port = port;
    config.host = host;
    config.ioThreads = ioThreads;
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]).substr(0, 12) == "--public-dir")
            publicDirOverridden = true;

    Logger::instance().setLevel(LogLevel::Info);
    hw::setWarnHook([](const char* message) { HTTP_LOG_WARN("%s", message); });

    // The embedded page is what the browser loaded, so it is what a redeploy changes: its hash is the build id (a
    // missing ./public would make every build "0", and open tabs would never reload after an upgrade).
    std::string embeddedBuildId;
#ifdef GSCOPE_EMBEDDED_INDEX_HTML
    if (!publicDirOverridden) {
        char text[32];
        std::snprintf(text, sizeof text, "%016zx",
                      std::hash<std::string_view>{}(embeddedGzip()));
        embeddedBuildId = text;
    }
#endif
    gscope::MetricsService metrics(
        forcedProvider.empty() ? gscope::detectProvider(intervalMs)
                               : gscope::createProvider(forcedProvider, intervalMs),
        historySize, publicDir, std::chrono::seconds(5), gscope::HostInfo::local(), embeddedBuildId);
    if (!metrics.start()) {
        std::fprintf(stderr, "failed to start metrics provider\n");
        return 1;
    }
    HTTP_LOG_INFO("metrics provider = %s", metrics.providerName());

    Server server(config);
    std::thread signalWaiter([&server, &stopSignals] {
        int signal = 0;
        sigwait(&stopSignals, &signal);
        server.stop();
    });

    server.onGet("/health", [](HttpContext& context) { context.text("ok"); });

    // The metrics: one librpc service (Monitor.Watch) on /rpc. A dashboard
    // past --clients waits for a worker, then is refused (Unavailable) and
    // retries with backoff.
    gscope::MonitorRpc monitor(metrics);
    rpc::Endpoint endpoint(rpc::EndpointOptions().workers(clients).queueCapacity(clients));
    endpoint.add(monitor).attach(server, "/rpc");

    // index.html's own filename never changes across a redeploy (unlike
    // the content-hashed JS/CSS it references), so it must not be cached
    // the way onStaticFiles() below caches everything else -- otherwise a
    // browser can sit on a stale index.html (and so a stale bundle
    // reference) well past the buildId check that's supposed to catch
    // exactly this. Read fresh on every request; it's a few KB.
    auto serveIndexHtml = [publicDir, publicDirOverridden, embeddedBuildId](HttpContext& context) {
#ifdef GSCOPE_EMBEDDED_INDEX_HTML
        // Default deployment: serve the copy objcopy linked into this very
        // binary, no filesystem access at all -- unless --public-dir asked
        // to override it with a build on disk instead.
        if (!publicDirOverridden) {
            // no-cache means "ask first": with an ETag, an unchanged page costs a 304, not a full download.
            const std::string etag = "\"" + embeddedBuildId + "\"";
            context.header("Cache-Control", "no-cache");
            context.header("ETag", etag);
            context.header("Vary", "Accept-Encoding");
            if (context.request().headers.get("If-None-Match") == etag) {
                context.status(304);
                return;
            }
            const bool gzip = context.request().headers.get("Accept-Encoding").find("gzip") != std::string_view::npos;
            if (gzip) {
                const std::string_view gz = embeddedGzip();
                context.header("Content-Encoding", "gzip");
                context.bytes(gz.data(), gz.size(), "text/html; charset=utf-8");
            } else {
                context.text(embeddedPlain(), "text/html; charset=utf-8");
            }
            return;
        }
#endif
        std::ifstream file(publicDir + "/index.html", std::ios::binary);
        if (!file) {
            context.status(404).text("Not Found");
            return;
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        context.header("Cache-Control", "no-cache");
        context.text(contents.str(), "text/html; charset=utf-8");
    };
    server.onGet("/", serveIndexHtml);
    server.onGet("/index.html", serveIndexHtml);
    server.onHead("/", serveIndexHtml);
    server.onHead("/index.html", serveIndexHtml);

#ifdef GSCOPE_EMBEDDED_INDEX_HTML
    // The embedded single-file bundle needs no other assets (JS/CSS are
    // folded into it); only register the disk-backed static handler -- which
    // requires publicDir to exist -- when there's a reason to (an explicit
    // --public-dir, e.g. a normal multi-file `ng build` output).
    if (publicDirOverridden || std::filesystem::is_directory(publicDir))
        server.onStaticFiles("/", publicDir);
#else
    server.onStaticFiles("/", publicDir);
#endif

    server.run();
    pthread_kill(signalWaiter.native_handle(), SIGTERM);   // stopped some other way: release the waiter
    signalWaiter.join();
    // Closing the watchers ends every Watch stream at once, so the endpoint
    // has nothing left to wait for.
    metrics.stop();
    endpoint.shutdown(std::chrono::seconds(2));
    Logger::instance().flush();
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
