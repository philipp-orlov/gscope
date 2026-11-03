import { TestBed } from '@angular/core/testing';

import { MetricsService } from './metrics.service';
import { MONITOR_CONNECT, MonitorConnection } from './monitor-connection';
import { InventoryInit, SampleInit, Update } from './wire/monitor.bufs';

/** A Watch call the test drives: push updates, end it, or fail it. */
class FakeConnection implements MonitorConnection {
  closed = false;
  private queue: (Update | Error | null)[] = [];
  private waiting: (() => void) | null = null;

  readonly updates: AsyncIterable<Update> = {
    [Symbol.asyncIterator]: () => ({
      next: async (): Promise<IteratorResult<Update>> => {
        while (this.queue.length === 0) await new Promise<void>((resolve) => (this.waiting = resolve));
        const item = this.queue.shift()!;
        if (item === null) return { done: true, value: undefined };
        if (item instanceof Error) throw item;
        return { done: false, value: item };
      },
    }),
  };

  push(inventory: InventoryInit | undefined, samples: SampleInit[]): void {
    this.feed(Update.decode(Update.encode({ inventory, samples })));
  }
  end(): void {
    this.feed(null);
  }
  fail(): void {
    this.feed(new Error('connection lost'));
  }
  close(): void {
    this.closed = true;
  }
  private feed(item: Update | Error | null): void {
    this.queue.push(item);
    this.waiting?.();
    this.waiting = null;
  }
}

const inventory = (buildId = 'v1'): InventoryInit => ({ hostname: 'h', kernel: 'k', provider: 'proc', cpuCount: 2, buildId, memTotalMib: 200 });
const sample = (utilizationCenti: number): SampleInit => ({ timestampMs: 1000, memUsedMib: 100, cores: [{ utilizationCenti, frequencyMhz: 1500 }] });

/** Lets the service's async loop catch up. */
const flush = async () => {
  for (let i = 0; i < 10; i++) await Promise.resolve();
};

describe('MetricsService', () => {
  let connections: FakeConnection[];
  let urls: string[];
  let refuse: boolean;

  beforeEach(() => {
    connections = [];
    urls = [];
    refuse = false;
    TestBed.configureTestingModule({
      providers: [
        {
          provide: MONITOR_CONNECT,
          useValue: async (url: string) => {
            urls.push(url);
            if (refuse) throw new Error('refused');
            const connection = new FakeConnection();
            connections.push(connection);
            return connection;
          },
        },
      ],
    });
  });

  afterEach(() => vi.useRealTimers());

  it('connects to /rpc on the page host and opens with the first update', async () => {
    const service = TestBed.inject(MetricsService);
    expect(service.status()).toBe('connecting');
    await flush();
    expect(urls[0]).toBe(`ws://${location.host}/rpc`);
    connections[0].push(inventory(), []);
    await flush();
    expect(service.status()).toBe('open');
  });

  it('takes the system info from the inventory', async () => {
    const service = TestBed.inject(MetricsService);
    await flush();
    connections[0].push(inventory(), []);
    await flush();
    expect(service.systemInfo()).toEqual({ hostname: 'h', kernel: 'k', provider: 'proc', cpuCount: 2 });
  });

  it('seeds history and latest from the backlog, then appends live samples', async () => {
    const service = TestBed.inject(MetricsService);
    await flush();
    connections[0].push(inventory(), [sample(1000), sample(2000)]);
    await flush();
    expect(service.history().map((s) => s.cpu.cores[0].utilization)).toEqual([10, 20]);
    connections[0].push(undefined, [sample(3000)]);
    await flush();
    expect(service.history().map((s) => s.cpu.cores[0].utilization)).toEqual([10, 20, 30]);
    expect(service.latest()?.cpu.cores[0].utilization).toBe(30);
    expect(service.latest()?.memory.totalMb).toBe(200);
  });

  it('caps history at 120 samples, dropping the oldest', async () => {
    const service = TestBed.inject(MetricsService);
    await flush();
    connections[0].push(inventory(), []);
    for (let i = 0; i < 125; i++) connections[0].push(undefined, [sample(i)]);
    await flush();
    await flush();
    await new Promise((r) => setTimeout(r));
    expect(service.history().length).toBe(120);
    expect(service.history()[0].cpu.cores[0].utilization).toBe(0.05);
  });

  it('reconnects with backoff when the call ends, and starts the history afresh', async () => {
    vi.useFakeTimers();
    const service = TestBed.inject(MetricsService);
    await flush();
    connections[0].push(inventory(), [sample(1000)]);
    await flush();
    connections[0].fail();
    await flush();
    expect(service.status()).toBe('closed');
    expect(connections[0].closed).toBe(true);
    expect(connections.length).toBe(1);
    await vi.advanceTimersByTimeAsync(1000);
    expect(connections.length).toBe(2);
    connections[1].push(inventory(), [sample(5000)]);
    await flush();
    expect(service.status()).toBe('open');
    expect(service.history().map((s) => s.cpu.cores[0].utilization)).toEqual([50]);
  });

  it('backs off further while the daemon refuses', async () => {
    vi.useFakeTimers();
    refuse = true;
    TestBed.inject(MetricsService);
    await flush();
    expect(urls.length).toBe(1);
    await vi.advanceTimersByTimeAsync(1000);
    expect(urls.length).toBe(2);
    await vi.advanceTimersByTimeAsync(1999);
    expect(urls.length).toBe(2);
    await vi.advanceTimersByTimeAsync(1);
    expect(urls.length).toBe(3);
  });

  describe('build id', () => {
    let reloadSpy: ReturnType<typeof vi.fn>;
    const originalLocation = window.location;
    beforeEach(() => {
      reloadSpy = vi.fn();
      Object.defineProperty(window, 'location', { value: { ...originalLocation, reload: reloadSpy }, writable: true, configurable: true });
    });
    afterEach(() => Object.defineProperty(window, 'location', { value: originalLocation, writable: true, configurable: true }));

    it('does not reload on the first build id (it only sets the baseline), nor on the same one', async () => {
      TestBed.inject(MetricsService);
      await flush();
      connections[0].push(inventory('v1'), []);
      connections[0].push(inventory('v1'), [sample(1)]);
      await flush();
      expect(reloadSpy).not.toHaveBeenCalled();
    });

    it('reloads once the server reports a different build id', async () => {
      TestBed.inject(MetricsService);
      await flush();
      connections[0].push(inventory('v1'), []);
      connections[0].push(inventory('v2'), [sample(1)]);
      await flush();
      expect(reloadSpy).toHaveBeenCalledTimes(1);
    });
  });

  it('closes the call when destroyed', async () => {
    const service = TestBed.inject(MetricsService);
    await flush();
    service.ngOnDestroy();
    expect(connections[0].closed).toBe(true);
  });
});
