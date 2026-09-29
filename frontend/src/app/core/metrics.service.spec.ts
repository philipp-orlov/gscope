import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { TestBed } from '@angular/core/testing';

import { MetricsService } from './metrics.service';
import { WEBSOCKET_FACTORY, WebSocketLike } from './websocket-factory';
import { Sample } from '../models/metrics.model';

class FakeSocket implements WebSocketLike {
  onopen: ((event: Event) => void) | null = null;
  onmessage: ((event: MessageEvent) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  closed = false;

  close(): void {
    this.closed = true;
    this.onclose?.(new CloseEvent('close'));
  }

  open(): void {
    this.onopen?.(new Event('open'));
  }

  emit(data: unknown): void {
    this.onmessage?.(new MessageEvent('message', { data: JSON.stringify(data) }));
  }
}

function makeSample(utilization: number): Sample {
  return {
    timestamp: 1000,
    cpu: { cores: [{ utilization, frequencyMhz: 1500 }] },
    memory: { usedMb: 100, totalMb: 200, swapUsedMb: 0, swapTotalMb: 0 },
    gpus: [],
    processes: [],
    network: { rxBytesPerSec: 0, txBytesPerSec: 0 },
    temperatures: [],
  };
}

describe('MetricsService', () => {
  let sockets: FakeSocket[];

  beforeEach(() => {
    sockets = [];
    TestBed.configureTestingModule({
      providers: [
        provideHttpClient(),
        provideHttpClientTesting(),
        {
          provide: WEBSOCKET_FACTORY,
          useValue: () => {
            const socket = new FakeSocket();
            sockets.push(socket);
            return socket;
          },
        },
      ],
    });

    // MetricsService fetches /api/system in its constructor; flush that
    // request in every test so it doesn't dangle into the next one.
    const http = TestBed.inject(HttpTestingController);
    TestBed.inject(MetricsService);
    http.expectOne('/api/system').flush({
      hostname: 'test-host',
      kernel: 'Linux 6.0',
      provider: 'proc',
      cpuCount: 4,
    });
  });

  afterEach(() => {
    TestBed.inject(HttpTestingController).verify();
  });

  it('starts in the connecting state and opens on socket connect', () => {
    const service = TestBed.inject(MetricsService);
    expect(service.status()).toBe('connecting');
    sockets[0].open();
    expect(service.status()).toBe('open');
  });

  it('fetches and exposes system info', () => {
    const service = TestBed.inject(MetricsService);
    expect(service.systemInfo()).toEqual({
      hostname: 'test-host',
      kernel: 'Linux 6.0',
      provider: 'proc',
      cpuCount: 4,
    });
  });

  it('seeds history and latest from a "history" backlog message', () => {
    const service = TestBed.inject(MetricsService);
    sockets[0].open();
    sockets[0].emit({ type: 'history', samples: [makeSample(10), makeSample(20)] });

    expect(service.history().length).toBe(2);
    expect(service.latest()?.cpu.cores[0].utilization).toBe(20);
  });

  it('appends a "sample" message to history and updates latest', () => {
    const service = TestBed.inject(MetricsService);
    sockets[0].open();
    sockets[0].emit({ type: 'history', samples: [makeSample(10)] });
    sockets[0].emit({ type: 'sample', sample: makeSample(30) });

    expect(service.history().length).toBe(2);
    expect(service.latest()?.cpu.cores[0].utilization).toBe(30);
  });

  it('caps history at 120 samples, dropping the oldest', () => {
    const service = TestBed.inject(MetricsService);
    sockets[0].open();
    for (let i = 0; i < 125; i++) {
      sockets[0].emit({ type: 'sample', sample: makeSample(i) });
    }
    const history = service.history();
    expect(history.length).toBe(120);
    expect(history[0].cpu.cores[0].utilization).toBe(5); // 0..4 dropped
    expect(history[119].cpu.cores[0].utilization).toBe(124);
  });

  it('ignores malformed frames instead of throwing', () => {
    const service = TestBed.inject(MetricsService);
    sockets[0].open();
    expect(() => sockets[0].onmessage?.(new MessageEvent('message', { data: '{not json' }))).not.toThrow();
    expect(service.latest()).toBeNull();
  });

  it('reconnects with backoff after the socket closes', () => {
    vi.useFakeTimers();
    const service = TestBed.inject(MetricsService);
    sockets[0].open();
    sockets[0].close();
    expect(service.status()).toBe('closed');
    expect(sockets.length).toBe(1);

    vi.advanceTimersByTime(1000);
    expect(sockets.length).toBe(2);
    vi.useRealTimers();
  });

  it('does not reload on the first buildId (it only sets the baseline)', () => {
    const reloadSpy = vi.fn();
    const originalLocation = window.location;
    Object.defineProperty(window, 'location', {
      value: { ...originalLocation, reload: reloadSpy },
      writable: true,
      configurable: true,
    });

    TestBed.inject(MetricsService);
    sockets[0].open();
    sockets[0].emit({ type: 'history', samples: [], buildId: 'v1' });
    sockets[0].emit({ type: 'sample', sample: makeSample(1), buildId: 'v1' });
    expect(reloadSpy).not.toHaveBeenCalled();

    Object.defineProperty(window, 'location', { value: originalLocation, writable: true, configurable: true });
  });

  it('reloads once the server reports a different buildId', () => {
    const reloadSpy = vi.fn();
    const originalLocation = window.location;
    Object.defineProperty(window, 'location', {
      value: { ...originalLocation, reload: reloadSpy },
      writable: true,
      configurable: true,
    });

    const service = TestBed.inject(MetricsService);
    sockets[0].open();
    sockets[0].emit({ type: 'history', samples: [], buildId: 'v1' });
    sockets[0].emit({ type: 'sample', sample: makeSample(1), buildId: 'v2' });

    expect(reloadSpy).toHaveBeenCalledTimes(1);
    // The message that carried the new buildId is not applied -- the page
    // is about to reload anyway, so there is no point updating state.
    expect(service.latest()).toBeNull();

    Object.defineProperty(window, 'location', { value: originalLocation, writable: true, configurable: true });
  });
});
