# gscope

A [jetson_stats](https://github.com/rbonghi/jetson_stats)-style system
monitor with a web UI instead of a terminal one: a C++ backend (built on
[libhttp](../libhttp)) samples CPU/GPU/memory/temperature and pushes updates over
a WebSocket; an Angular + Angular Material frontend renders them as a live
dashboard.

```
gscope/
  backend/   C++ HTTP/WebSocket server (gscope-daemon), see backend/README.md
  frontend/  Angular dashboard, see frontend/README.md
```

## Quick start (development)

Two processes, one proxying to the other:

```sh
# Terminal 1 -- the backend, on 8081
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target gscope-daemon
./build/backend/gscope-daemon --port 8081

# Terminal 2 -- the frontend dev server, proxying /api and /ws to 8081
cd frontend
npm install
npm start   # ng serve; see frontend/proxy.conf.json
```

Then open http://localhost:4200.

## Production

Build the frontend and point the backend at the result:

```sh
cd frontend && npm run build
./gscope-daemon --public-dir frontend/dist/frontend/browser
```

One process, one port, no separate dev server or proxy.

### Single-file deployment (the default)

For copying to a device as one artifact instead of a directory, `npm run
build:single` folds the hashed JS bundle, CSS, and favicon that `ng build`
produces back into one dependency-free `index.html`
(`frontend/dist/frontend/single/index.html`). Copied into
`backend/public/index.html`, this is exactly what `gscope-daemon` embeds into
itself at build time (via `objcopy` -- see `backend/README.md`), so a
rebuilt `gscope-daemon` alone -- no sibling `public/` directory -- already
serves this UI by default:

```sh
cd frontend && npm run build:single
cp dist/frontend/single/index.html ../backend/public/index.html
cmake --build build --target gscope-daemon
./gscope-daemon   # no --public-dir needed
```

To serve a *different* build without recompiling the daemon (or a normal,
non-single-file `ng build` output), point `--public-dir` at it instead --
that still reads from disk exactly as before:

```sh
mkdir -p /path/to/public && cp dist/frontend/single/index.html /path/to/public/
./gscope-daemon --public-dir /path/to/public
```

Only the Google Fonts stylesheets stay external (Roboto/Material Icons);
everything else -- app JS, component styles, theme CSS, favicon -- lives
inside that single HTML file.

## Design

- **The provider hook.** The backend never assumes it's on a Jetson: it
  detects `tegrastats`, prefers NVML for a discrete NVIDIA GPU (one direct
  library call per metric, `dlopen`'d at runtime so it doesn't affect Jetson
  compilation), falls back to shelling out to `nvidia-smi` if NVML didn't
  load, and falls back again to a `/proc`-only reading (CPU/RAM, no GPU) if
  none of those are present -- the same ladder nvtop/btop use. See
  `backend/include/gscope/provider.hpp`.
- **One JSON schema, three uses.** A `Sample` becomes JSON once
  (`json_codec.cpp`) and that same text is what the REST endpoints return,
  what's buffered for the "history" backlog a new WebSocket client gets, and
  what's broadcast live -- never re-encoded per connection.
- **Lightweight by construction.** The backend samples on the provider's own
  cadence and pushes only on change; the frontend's chart redraws only when
  new data arrives (no `requestAnimationFrame` loop ticking on an idle tab).
- **One binary, no assets to lose.** `public/index.html` (the single-file
  build) is linked into `gscope-daemon` itself via `objcopy`, so copying just
  the executable to a device already gets a working UI; `--public-dir`
  overrides it with an on-disk build when wanted. See `backend/README.md`.
