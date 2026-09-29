// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// gscope backend: a jetson_stats-style system monitor, served over
// HTTP/WebSocket instead of a terminal UI. See ../README.md for the wire
// protocol and how this pairs with the Angular frontend.
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/utsname.h>

#include "http/logger.hpp"
#include "http/server.hpp"
#include "gscope/metrics_service.hpp"
#include "gscope/provider.hpp"
#include "json/json.hpp"

using namespace http;
using json::Json;

#ifdef GSCOPE_EMBEDDED_INDEX_HTML
// Defined by objcopy (see CMakeLists.txt) from public/index.html; not real
// pointers into an array, just symbols marking where the linker placed the
// embedded bytes in the binary's .data section.
extern "C" {
extern const char _binary_index_html_start[];
extern const char _binary_index_html_end[];
}
#endif

namespace {
volatile std::sig_atomic_t stopRequested = 0;
void handleSignal(int)
{
    stopRequested = 1;
}

std::string hostName()
{
    char buffer[256] = {0};
    if (gethostname(buffer, sizeof(buffer) - 1) != 0)
        return "unknown";
    return buffer;
}

std::string kernelRelease()
{
    struct utsname info;
    if (uname(&info) != 0)
        return "unknown";
    return std::string(info.sysname) + " " + info.release;
}

unsigned parseUnsigned(std::string_view value, const char* optionName)
{
    unsigned result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument(std::string("invalid value for ") + optionName);
    return result;
}

}  // namespace

int main(int argc, char** argv)
try {
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

    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--help") {
            std::puts(
                "gscope-daemon [--port PORT] [--interval-ms MS] [--history-size N] "
                "[--provider tegrastats|nvml|nvidia-smi|proc] [--public-dir DIR] "
                "[--io-threads N]");
            return 0;
        }
        if (index + 1 == argc)
            throw std::invalid_argument("missing value for " + std::string(option));
        const std::string_view value(argv[++index]);

        if (option == "--port")
            config.port = static_cast<uint16_t>(parseUnsigned(value, "--port"));
        else if (option == "--interval-ms")
            intervalMs = parseUnsigned(value, "--interval-ms");
        else if (option == "--history-size")
            historySize = parseUnsigned(value, "--history-size");
        else if (option == "--provider")
            forcedProvider = value;
        else if (option == "--public-dir") {
            publicDir = value;
            publicDirOverridden = true;
        } else if (option == "--io-threads")
            config.ioThreads = parseUnsigned(value, "--io-threads");
        else
            throw std::invalid_argument("unknown argument: " + std::string(option));
    }

    Logger::instance().setLevel(LogLevel::Info);

    gscope::MetricsService metrics(
        forcedProvider.empty() ? gscope::detectProvider(intervalMs)
                               : gscope::createProvider(forcedProvider, intervalMs),
        historySize, publicDir);
    if (!metrics.start()) {
        std::fprintf(stderr, "failed to start metrics provider\n");
        return 1;
    }
    HTTP_LOG_INFO("metrics provider = %s", metrics.providerName());

    Server server(config);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    server.onGet("/health", [](HttpContext& context) { context.json(Json{{"status", "ok"}}); });

    server.onGet("/api/system", [&metrics](HttpContext& context) {
        context.json(Json{
            {"hostname", hostName()},
            {"kernel", kernelRelease()},
            {"provider", metrics.providerName()},
            {"cpuCount", static_cast<long long>(sysconf(_SC_NPROCESSORS_ONLN))},
        });
    });

    server.onGet("/api/metrics/latest", [&metrics](HttpContext& context) {
        std::string body = metrics.latestJson();
        context.text(body, "application/json");
    });

    server.onGet("/api/metrics/history", [&metrics](HttpContext& context) {
        std::string body = metrics.historyJson();
        context.text(body, "application/json");
    });

    server.onWebSocket("/ws/metrics", [&metrics](std::shared_ptr<WebSocketConnection> connection) {
        HTTP_LOG_INFO("client connected (%s)", connection->remoteAddress().c_str());
        metrics.addSubscriber(connection);
        std::string remoteAddress = connection->remoteAddress();
        connection->onClose([remoteAddress]() {
            HTTP_LOG_INFO("client disconnected (%s)", remoteAddress.c_str());
        });
    });

    // index.html's own filename never changes across a redeploy (unlike
    // the content-hashed JS/CSS it references), so it must not be cached
    // the way onStaticFiles() below caches everything else -- otherwise a
    // browser can sit on a stale index.html (and so a stale bundle
    // reference) well past the buildId check that's supposed to catch
    // exactly this. Read fresh on every request; it's a few KB.
    auto serveIndexHtml = [publicDir, publicDirOverridden](HttpContext& context) {
#ifdef GSCOPE_EMBEDDED_INDEX_HTML
        // Default deployment: serve the copy objcopy linked into this very
        // binary, no filesystem access at all -- unless --public-dir asked
        // to override it with a build on disk instead.
        if (!publicDirOverridden) {
            context.header("Cache-Control", "no-cache");
            context.text(std::string_view(_binary_index_html_start,
                                          _binary_index_html_end - _binary_index_html_start),
                        "text/html; charset=utf-8");
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

    server.run([] { return stopRequested != 0; });
    metrics.stop();
    Logger::instance().flush();
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
