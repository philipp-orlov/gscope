import { Injectable, OnDestroy, inject, signal } from '@angular/core';

import { MetricsDecoder } from './metrics-decoder';
import { MONITOR_CONNECT, MonitorConnection } from './monitor-connection';
import { ConnectionStatus, Sample, SystemInfo } from '../models/metrics.model';

export const HISTORY_CAPACITY = 120;
const INITIAL_RECONNECT_DELAY_MS = 1000;
const MAX_RECONNECT_DELAY_MS = 30000;

// Owns the live connection to the daemon: one librpc call, Monitor.Watch on
// /rpc, whose first update brings the backlog and the inventory (host, GPU
// and sensor names) and every later one a sample. Buffers the last
// HISTORY_CAPACITY samples for the charts, exposes the latest one for the
// numeric readouts, and reconnects with exponential backoff if the call ends
// (the backend restarting, Wi-Fi hiccup, a too slow tab).
@Injectable({ providedIn: 'root' })
export class MetricsService implements OnDestroy {
  private readonly connect = inject(MONITOR_CONNECT);
  private readonly decoder = new MetricsDecoder();

  private connection: MonitorConnection | null = null;
  private reconnectDelayMs = INITIAL_RECONNECT_DELAY_MS;
  private reconnectTimer?: ReturnType<typeof setTimeout>;
  private wake: (() => void) | null = null;
  private destroyed = false;
  // Set from the first inventory's build_id; a later one that differs means
  // the backend now serves a redeployed frontend -- reload to get its fresh
  // JS/CSS rather than keep running the stale one.
  private knownBuildId: string | null = null;

  readonly status = signal<ConnectionStatus>('connecting');
  readonly latest = signal<Sample | null>(null);
  readonly history = signal<Sample[]>([]);
  readonly systemInfo = signal<SystemInfo | null>(null);

  constructor() {
    void this.run();
  }

  ngOnDestroy(): void {
    this.destroyed = true;
    clearTimeout(this.reconnectTimer);
    this.wake?.();
    this.connection?.close();
  }

  private url(): string {
    const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
    return `${protocol}://${location.host}/rpc`;
  }

  private async run(): Promise<void> {
    while (!this.destroyed) {
      this.status.set('connecting');
      try {
        this.connection = await this.connect(this.url());
        if (this.destroyed) break;
        this.decoder.reset();
        let first = true;
        for await (const update of this.connection.updates) {
          if (this.handle(update, first)) return;
          if (first) {
            first = false;
            this.status.set('open');
            this.reconnectDelayMs = INITIAL_RECONNECT_DELAY_MS;
          }
        }
      } catch {
        // connection refused or lost: retry below
      } finally {
        this.connection?.close();
        this.connection = null;
      }
      if (this.destroyed) break;
      this.status.set('closed');
      await this.sleep(this.reconnectDelayMs);
      this.reconnectDelayMs = Math.min(this.reconnectDelayMs * 2, MAX_RECONNECT_DELAY_MS);
    }
  }

  private sleep(ms: number): Promise<void> {
    return new Promise((resolve) => {
      this.wake = resolve;
      this.reconnectTimer = setTimeout(resolve, ms);
    });
  }

  /** True when the page reloads (a new frontend build): stop here. */
  private handle(update: Parameters<MetricsDecoder['apply']>[0], first: boolean): boolean {
    const decoded = this.decoder.apply(update);
    if (decoded.system) this.systemInfo.set(decoded.system);
    if (decoded.buildId !== null && this.checkBuildId(decoded.buildId)) return true;
    if (decoded.samples.length > 0) {
      // The first update of a connection starts the history afresh (the
      // backlog); the rest of a long backlog and every live update append.
      this.history.update((existing) => {
        const next = first ? decoded.samples : [...existing, ...decoded.samples];
        return next.length > HISTORY_CAPACITY ? next.slice(next.length - HISTORY_CAPACITY) : next;
      });
      this.latest.set(decoded.samples[decoded.samples.length - 1]);
    } else if (first) {
      this.history.set([]);
    }
    return false;
  }

  // True if the server reports a different frontend build than the one this
  // page loaded with -- and reloads to fetch it. The first build_id only sets
  // the baseline (also across reconnects, so a restart of the same build does
  // not reload).
  private checkBuildId(buildId: string): boolean {
    if (this.knownBuildId === null) {
      this.knownBuildId = buildId;
      return false;
    }
    if (buildId === this.knownBuildId) return false;
    location.reload();
    return true;
  }
}
