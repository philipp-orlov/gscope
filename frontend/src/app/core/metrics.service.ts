import { HttpClient } from '@angular/common/http';
import { Injectable, OnDestroy, inject, signal } from '@angular/core';

import { WEBSOCKET_FACTORY, WebSocketLike } from './websocket-factory';
import { ConnectionStatus, Sample, SystemInfo, WireMessage } from '../models/metrics.model';

const HISTORY_CAPACITY = 120;
const INITIAL_RECONNECT_DELAY_MS = 1000;
const MAX_RECONNECT_DELAY_MS = 30000;

// Owns the live WebSocket connection to /ws/metrics: buffers the last
// HISTORY_CAPACITY samples for the charts, exposes the latest one for the
// numeric readouts, and reconnects with exponential backoff if the
// connection drops (the backend restarting, Wi-Fi hiccup, etc).
@Injectable({ providedIn: 'root' })
export class MetricsService implements OnDestroy {
  private readonly http = inject(HttpClient);
  private readonly createWebSocket = inject(WEBSOCKET_FACTORY);

  private socket?: WebSocketLike;
  private reconnectDelayMs = INITIAL_RECONNECT_DELAY_MS;
  private reconnectTimer?: ReturnType<typeof setTimeout>;
  private destroyed = false;
  // Set from the first message's buildId; a later message with a
  // different one means the backend is now serving a redeployed frontend
  // build (see backend/README.md's "buildId" wire field) -- reload to get
  // its fresh JS/CSS rather than keep running the stale one.
  private knownBuildId: string | null = null;

  readonly status = signal<ConnectionStatus>('connecting');
  readonly latest = signal<Sample | null>(null);
  readonly history = signal<Sample[]>([]);
  readonly systemInfo = signal<SystemInfo | null>(null);

  constructor() {
    this.fetchSystemInfo();
    this.connect();
  }

  ngOnDestroy(): void {
    this.destroyed = true;
    clearTimeout(this.reconnectTimer);
    this.socket?.close();
  }

  private fetchSystemInfo(): void {
    this.http.get<SystemInfo>('/api/system').subscribe({
      next: (info) => this.systemInfo.set(info),
      // System info is a nice-to-have header, not on the critical path --
      // a fetch failure shouldn't stop the live metrics from working.
      error: () => undefined,
    });
  }

  private connect(): void {
    if (this.destroyed) return;

    const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
    this.socket = this.createWebSocket(`${protocol}://${location.host}/ws/metrics`);
    this.status.set('connecting');

    this.socket.onopen = () => {
      this.status.set('open');
      this.reconnectDelayMs = INITIAL_RECONNECT_DELAY_MS;
    };
    this.socket.onmessage = (event: MessageEvent) => this.handleMessage(event.data);
    this.socket.onclose = () => {
      this.status.set('closed');
      this.scheduleReconnect();
    };
    this.socket.onerror = () => this.socket?.close();
  }

  private scheduleReconnect(): void {
    if (this.destroyed) return;
    clearTimeout(this.reconnectTimer);
    this.reconnectTimer = setTimeout(() => this.connect(), this.reconnectDelayMs);
    this.reconnectDelayMs = Math.min(this.reconnectDelayMs * 2, MAX_RECONNECT_DELAY_MS);
  }

  private handleMessage(raw: string): void {
    let message: WireMessage;
    try {
      message = JSON.parse(raw) as WireMessage;
    } catch {
      return; // malformed frame; drop it rather than crash the dashboard
    }

    if (message.buildId && this.checkBuildId(message.buildId)) return;

    if (message.type === 'history') {
      const samples = message.samples.slice(-HISTORY_CAPACITY);
      this.history.set(samples);
      if (samples.length > 0) this.latest.set(samples[samples.length - 1]);
    } else if (message.type === 'sample') {
      this.latest.set(message.sample);
      this.history.update((existing) => {
        const next = [...existing, message.sample];
        return next.length > HISTORY_CAPACITY ? next.slice(next.length - HISTORY_CAPACITY) : next;
      });
    }
  }

  // Returns true if the server just reported a different frontend build
  // than the one this page loaded with -- and reloads to fetch it. False
  // (including on the very first message, which only sets the baseline)
  // means the caller should keep processing the message as normal.
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
