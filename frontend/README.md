# gscope frontend

An Angular + Angular Material dashboard for [../backend](../backend). Live
data over a WebSocket (`/ws/metrics`); no polling.

## Setup

```sh
npm install
```

## Development

```sh
npm start   # ng serve --proxy-config proxy.conf.json (wired in angular.json)
```

Serves on http://localhost:4200 and proxies `/api/*` and `/ws/*` to the
backend on `http://localhost:8081` (see `proxy.conf.json`) -- start the
backend separately (`../backend/README.md`).

## Testing

```sh
npm test    # ng test -- vitest, jsdom, no browser/Chromium install needed
```

## Production build

```sh
npm run build
```

Output goes to `dist/frontend/browser/`. Point the backend at it directly:
```sh
./gscope-daemon --public-dir frontend/dist/frontend/browser
```

## Structure

```
src/app/
  models/metrics.model.ts       Wire types shared with the backend (see backend/README.md)
  core/
    websocket-factory.ts        Injectable WebSocket construction (the test seam)
    metrics.service.ts          Owns the WebSocket connection, history buffer, reconnect/backoff
  shared/sparkline-chart/       Canvas-based compact multi-series line chart
  dashboard/                    The page: CPU/memory/per-GPU cards + temperature chips
```

## Notes

- **Lightweight charts.** `SparklineChart` redraws only when its `series`
  input changes (an Angular `effect()`, not a render loop), so an idle tab
  costs nothing between WebSocket pushes. `computePoints()` (the
  value-to-pixel mapping) is a pure function, tested directly without a
  canvas.
- **More than one GPU.** The dashboard renders one card (and one chart) per
  entry in the latest sample's `gpus` array -- nothing is hardcoded to a
  single device.
- **Auto-updates on redeploy.** `MetricsService` remembers the `buildId` on
  the first WebSocket message and calls `location.reload()` the moment a
  later one carries a different value (see backend/README.md's "buildId").
  A redeployed frontend build reaches every open tab without anyone hitting
  refresh.
- **No `@angular/animations`.** Material 3's theming here is CSS-driven and
  the package is deprecated upstream in favor of `animate.enter`/`leave`;
  pulling it in would only add weight.
