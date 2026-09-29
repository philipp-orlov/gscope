# gscope backend

A jetson_stats-style metrics server built on [libhttp](../../libhttp): samples
CPU/GPU/memory/temperature on a background thread and serves them over REST
and a WebSocket.

## Building

Built from the top-level `gscope` CMake project, which adds a sibling `../libhttp`
checkout (`-DGSCOPE_LIBHTTP_DIR=...` to point elsewhere) unless the `http` target
already exists:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target gscope-daemon -j
ctest --test-dir build -R gscope
```

## Running

```sh
./gscope-daemon --port 8081 --interval-ms 1000 --history-size 120
```

| Flag | Default | Meaning |
|---|---|---|
| `--port` | 8081 | HTTP/WebSocket listen port |
| `--interval-ms` | 1000 | Sampling interval passed to the provider |
| `--history-size` | 120 | Samples buffered for a new client's backlog |
| `--provider` | autodetect | Force `tegrastats`, `nvml`, `nvidia-smi` or `proc` |
| `--public-dir` | *(embedded)* | Serve an on-disk Angular build instead of the copy built into the binary |
| `--io-threads` | 2 | Reactor threads for the HTTP/WebSocket server (see `ServerConfig::ioThreads`); the library default is one per CPU core, overkill for the handful of clients a metrics dashboard serves |

### The embedded default UI

`public/index.html` (the frontend's `npm run build:single` output -- see
`frontend/README.md`) is linked straight into `gscope-daemon` at build time via
`objcopy -I binary`, so the single executable serves a working UI out of the
box with no `public/` directory alongside it: `/` and `/index.html` come
from the binary's own `.data` section (see `CMakeLists.txt` and the
`GSCOPE_EMBEDDED_INDEX_HTML` block in `main.cpp`). Pass `--public-dir DIR` to
opt back into serving from disk instead -- e.g. a normal (non-single-file)
`ng build` output, or a redeployed `public/` without recompiling the daemon;
that's unchanged from before and still needs `DIR` to exist. Requires GNU
binutils (`objcopy`/`objdump`) at build time; on a toolchain without them
the build falls back to disk-only serving and `--public-dir`'s default
`./public` applies exactly as it always did (a warning is printed at
configure time when this happens).

## Providers -- the platform hook

`gscope::detectProvider()` (`include/gscope/provider.hpp`) picks, in order:

1. **`tegrastats`** (Jetson): parses tegrastats' one-line-per-sample text
   output. CPU per-core %/freq, RAM+SWAP, and the integrated GPU's
   utilization/temperature come from one line; GPU memory mirrors system RAM
   since Jetson's GPU uses unified memory.
2. **`nvml`** (a machine with a discrete NVIDIA GPU and a loadable driver):
   one direct NVML library call per metric instead of a subprocess + CSV
   parse per sample. `libnvidia-ml.so.1` (ships with every NVIDIA Linux
   driver package, the same one `nvidia-smi` itself links against) is
   `dlopen`'d/`dlsym`'d at runtime -- no nvml.h/CUDA Toolkit dependency, so
   this compiles unconditionally even on Jetson, where the `dlopen` simply
   fails and detection falls through. CPU/RAM come from `/proc`. A unified-
   memory NVIDIA SoC (e.g. GB10/Grace-Blackwell) reports GPU memory as
   unsupported, same as Jetson -- mirrored from system RAM the same way.
3. **`nvidia-smi`** (NVML didn't load but the CLI is on `PATH`): shells out
   to `nvidia-smi --query-gpu=... --format=csv` in looping mode, the same
   way `nvtop` used to before it linked NVML. CPU/RAM come from `/proc`.
4. **`proc`** (anything else): `/proc/stat` + `/proc/meminfo` only, no GPU
   data -- always available, so a provider is never absent.

### High CPU from the nvml/nvidia-smi provider? Check persistence mode

If the daemon's sampling thread is burning noticeably more than a rounding
error of CPU (e.g. ~10% of a core on an idle multi-GPU box), check
`nvidia-smi --query-gpu=index,persistence_mode,pstate --format=csv`. With
persistence mode `Disabled`, the kernel driver tears down and reinitializes
GPU driver state between queries once the GPU drops to an idle power state
(`P8`), so every NVML/`nvidia-smi` poll -- one per GPU per sampling
interval -- pays that wake/reinit cost synchronously in the polling thread.
This is a driver behavior, not something `gscope-daemon`'s code can avoid.
Fix it on the host: `sudo nvidia-smi -pm 1` (until reboot) or
`sudo systemctl enable --now nvidia-persistenced` (persists across
reboots); either keeps the driver context resident so NVML calls stay in
the microseconds range. If that's not an option, raising `--interval-ms`
reduces the polling frequency and thus the CPU cost proportionally.

Set `GSCOPE_PROVIDER=tegrastats|nvml|nvidia-smi|proc` to override detection (or
pass `--provider`). Each provider is a `gscope::MetricsProvider`
(`include/gscope/provider.hpp`); adding a new one (e.g. an AMD/Intel GPU
source) means implementing that interface and adding a branch to
`createProvider()`.

## Deploying as a systemd service

`gscope-daemon` is a single self-contained executable (the Angular UI is
embedded at build time -- see "The embedded default UI" above), so
deploying to a target machine is just copying that one file over; no
`public/` directory, no Node/npm on the target.

```sh
scp build/backend/gscope-daemon host:/tmp/
scp backend/scripts/install-service.sh host:/tmp/
ssh host sudo GSCOPE_PORT=80 /tmp/install-service.sh /tmp/gscope-daemon
```

`scripts/install-service.sh` installs the binary to
`/usr/local/bin/gscope-daemon`, creates a dedicated unprivileged `gscope` system
user (unless overridden via `GSCOPE_USER`), and registers/starts a
`gscope-daemon.service` systemd unit. To listen on a low port (e.g. the
default HTTP port 80, `GSCOPE_PORT=80`) without running the daemon as root,
the unit grants only `CAP_NET_BIND_SERVICE` via systemd's
`AmbientCapabilities` -- the same least-privilege technique used by
packaged nginx/caddy units. See the script's header comment for all
`GSCOPE_*` environment variable overrides (port, interval, history size,
service user). Re-running the script re-installs the binary and unit and
restarts the service, so it doubles as the upgrade path.

## Wire protocol

`GET /api/system` -- static info:
```json
{"hostname": "...", "kernel": "Linux 5.10...", "provider": "tegrastats", "cpuCount": 8}
```

`GET /api/metrics/latest` -- the most recent sample (empty `{}` before the
first reading). `GET /api/metrics/history` -- a JSON array of the buffered
samples, oldest first. A sample:
```json
{
  "timestamp": 1758500000000,
  "cpu": {"cores": [{"utilization": 12.0, "frequencyMhz": 1984.0}, ...]},
  "memory": {"usedMb": 4461, "totalMb": 15524, "swapUsedMb": 34, "swapTotalMb": 7762},
  "gpus": [{"name": "GPU0", "utilization": 0.0, "memoryUsedMb": 4461, "memoryTotalMb": 15524,
            "temperatureC": 50.7, "powerMw": -1, "frequencyMhz": 0}],
  "processes": [{"pid": 1234, "name": "chrome", "cpu": 5.2, "memoryMb": 214.0}, ...],
  "network": {"rxBytesPerSec": 125000.0, "txBytesPerSec": 8200.0},
  "temperatures": [{"name": "CPU", "celsius": 52.75}, ...]
}
```
`temperatureC`/`powerMw` of `-1000`/`-1` mean "sensor not available". Percent
fields (`cpu.cores[].utilization`, `gpus[].utilization`, `processes[].cpu`)
are rounded to 2 decimal places once in `json_codec.cpp`, the single place a
`Sample` becomes JSON -- every consumer (REST, WS backlog, live broadcast)
sees the same rounded value. `temperatures` comes from
`/sys/class/thermal/thermal_zone*` (generic Linux sensor framework, e.g.
`"acpitz 0"`) via `ProcCpuMemSampler`, so it's populated regardless of which
GPU provider is active -- tegrastats supplies its own CPU/GPU readings from
tegrastats' own text output instead. Empty if the machine/container exposes
no thermal zones.
`processes` is the top 8 by CPU% (`gscope::ProcessSampler`, `/proc`-based --
available regardless of which `MetricsProvider` is active), `cpu` relative
to one core the way `top`/`ps` report it (so a multi-threaded process can
read over 100%). `network` (`gscope::NetworkSampler`, also `/proc`-based) sums
every interface except loopback.

`GET /ws/metrics` (WebSocket) -- on connect, one backlog message:
```json
{"type": "history", "samples": [ <sample>, ... ], "buildId": "e3d5027cbda71af3"}
```
then one message per new reading:
```json
{"type": "sample", "sample": <sample>, "buildId": "e3d5027cbda71af3"}
```
`buildId` is a fingerprint (name+size+mtime) of `--public-dir`'s files,
recomputed at most once every 5s. The frontend (`MetricsService`) remembers
the first one it sees and calls `location.reload()` the moment a later
message carries a different one -- so redeploying the Angular build while a
tab is open gets picked up automatically, no manual refresh needed. `/` and
`/index.html` are served with `Cache-Control: no-cache` (unlike the
content-hashed JS/CSS, which keep the default long-lived caching) precisely
so that reload actually fetches the new `index.html` rather than a cached
stale one.

## Tests

`ctest -R gscope` runs, all without hardware or a running server:

| Test | What it covers |
|---|---|
| `gscope_tegrastats_parser` | Parsing a captured real tegrastats line (RAM/SWAP/CPU/GR3D/temperatures) and rejecting garbage |
| `gscope_tegrastats_provider_stream` | `TegrastatsProvider`'s real background thread + read loop, driven by an injected `VectorLineStream` instead of the real binary |
| `gscope_nvidia_smi_parser` | Parsing nvidia-smi CSV rows, including `[N/A]` fields |
| `gscope_nvidia_smi_provider_stream` | `NvidiaSmiProvider`'s real background thread + read loop, driven by an injected `VectorLineStream`; covers a unified-memory GPU's `[N/A]` memory fields getting mirrored from system RAM |
| `gscope_nvml_provider` | `nvmlFillGpuSample()`'s field dispatch and NaN fallback, plus `NvmlProvider`'s real background thread driven by an injected fake `NvmlApi` (no real driver needed); covers the same unified-memory mirroring |
| `gscope_proc_provider` | `/proc/stat` per-core utilization deltas and `/proc/meminfo` parsing from synthetic content |
| `gscope_process_sampler` | `/proc/[pid]/stat` + `/status` parsing, %CPU-from-ticks math, and a light integration check against the real `/proc` |
| `gscope_network_sampler` | `/proc/net/dev` parsing, loopback exclusion, and a light integration check against the real `/proc` |
| `gscope_frontend_version` | Directory fingerprint changes with content/mtime, stable when unchanged, `"0"` for a missing/empty directory |
| `gscope_history_buffer` | Ring buffer capacity and eviction order |
| `gscope_metrics_service` | End-to-end with a fake provider and a `WebSocketConnection` bound to in-memory send/post functions: history backlog, live broadcast, eviction, and `buildId` changing when the (temp-dir) deploy changes |

Providers that spawn a subprocess (tegrastats, nvidia-smi) take an
injectable `LineStreamFactory` (`include/gscope/line_stream.hpp`) so their read
loops are testable with canned lines too, without the real binaries -- see
`gscope_tegrastats_provider_stream` above.
