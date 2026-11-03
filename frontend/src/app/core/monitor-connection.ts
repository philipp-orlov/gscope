import { InjectionToken } from '@angular/core';

import { RpcClient } from './librpc/index';
import { MonitorClient, Update } from './wire/monitor.bufs';

/** One Monitor.Watch call: its updates, and a way to end it. */
export interface MonitorConnection {
  /** Ends when the server ends the call; throws (an RpcError) when the connection is lost. */
  updates: AsyncIterable<Update>;
  close(): void;
}

export type MonitorConnect = (url: string) => Promise<MonitorConnection>;

/** A librpc connection to `url` (ws://host/rpc) and its Watch stream. */
export async function connectMonitor(url: string): Promise<MonitorConnection> {
  const rpc = await RpcClient.connect(url);
  const stream = new MonitorClient(rpc).watch();
  return {
    updates: stream,
    close: () => {
      stream.cancel();
      rpc.close();
    },
  };
}

/** How MetricsService reaches the daemon; tests give a fake. */
export const MONITOR_CONNECT = new InjectionToken<MonitorConnect>('MONITOR_CONNECT', {
  providedIn: 'root',
  factory: () => connectMonitor,
});
