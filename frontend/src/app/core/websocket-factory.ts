import { InjectionToken } from '@angular/core';

// The minimal surface MetricsService needs from a WebSocket -- lets tests
// substitute a fake without a real socket/network.
export interface WebSocketLike {
  onopen: ((event: Event) => void) | null;
  onmessage: ((event: MessageEvent) => void) | null;
  onclose: ((event: CloseEvent) => void) | null;
  onerror: ((event: Event) => void) | null;
  close(): void;
}

export type WebSocketFactory = (url: string) => WebSocketLike;

export const WEBSOCKET_FACTORY = new InjectionToken<WebSocketFactory>('WEBSOCKET_FACTORY', {
  providedIn: 'root',
  factory: () => (url: string) => new WebSocket(url),
});
