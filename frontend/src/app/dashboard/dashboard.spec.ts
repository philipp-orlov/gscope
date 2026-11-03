import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';

import {
  Dashboard,
  average,
  cpuUtilizationNow,
  cpuUtilizationSeries,
  gpuCount,
  gpuTemperatureSeries,
  gpuUtilizationSeries,
  memoryPercentNow,
  memoryPercentSeries,
  networkChartMax,
  networkRxSeries,
  networkTotalBytesPerSec,
  networkTxSeries,
  swapPercentSeries,
  temperatureSeries,
} from './dashboard';
import { MetricsService } from '../core/metrics.service';
import { Sample } from '../models/metrics.model';

function makeSample(overrides: Partial<Sample> = {}): Sample {
  return {
    timestamp: 0,
    cpu: {
      cores: [
        { utilization: 10, frequencyMhz: 1000 },
        { utilization: 30, frequencyMhz: 1000 },
      ],
    },
    memory: { usedMb: 400, totalMb: 800, swapUsedMb: 10, swapTotalMb: 100 },
    gpus: [
      {
        name: 'GPU0',
        utilization: 20,
        memoryUsedMb: 100,
        memoryTotalMb: 200,
        temperatureC: 45,
        powerMw: 5000,
        frequencyMhz: 900,
      },
    ],
    processes: [{ pid: 1, name: 'init', cpu: 5, memoryMb: 12 }],
    network: { rxBytesPerSec: 1024 * 1024, txBytesPerSec: 512 * 1024 },
    temperatures: [{ name: 'CPU', celsius: 55 }],
    ...overrides,
  };
}

describe('dashboard data helpers', () => {
  it('average of empty array is 0', () => {
    expect(average([])).toBe(0);
  });

  it('averages CPU core utilization per sample', () => {
    expect(cpuUtilizationSeries([makeSample()])).toEqual([20]); // (10 + 30) / 2
  });

  it('reads a named temperature sensor, a gap (null) when absent', () => {
    expect(temperatureSeries([makeSample()], 'CPU')).toEqual([55]);
    expect(temperatureSeries([makeSample()], 'GPU')).toEqual([null]);
  });

  it('computes memory and swap percentages', () => {
    expect(memoryPercentSeries([makeSample()])).toEqual([50]);
    expect(swapPercentSeries([makeSample()])).toEqual([10]);
  });

  it('counts GPUs from the most recent sample', () => {
    expect(gpuCount([makeSample(), makeSample({ gpus: [] })])).toBe(0);
    expect(gpuCount([makeSample()])).toBe(1);
  });

  it('extracts per-GPU utilization and temperature series', () => {
    expect(gpuUtilizationSeries([makeSample()], 0)).toEqual([20]);
    expect(gpuTemperatureSeries([makeSample()], 0)).toEqual([45]);
  });

  it('charts the GPU temperature sentinel as a gap, neither -1000 nor 0', () => {
    const sample = makeSample({
      gpus: [
        {
          name: 'GPU0',
          utilization: 0,
          memoryUsedMb: 0,
          memoryTotalMb: 0,
          temperatureC: -1000,
          powerMw: -1,
          frequencyMhz: 0,
        },
      ],
    });
    expect(gpuTemperatureSeries([sample], 0)).toEqual([null]);
  });
});

describe('network series', () => {
  it('converts bytes/sec to MiB/sec', () => {
    const sample = makeSample({ network: { rxBytesPerSec: 2 * 1024 * 1024, txBytesPerSec: 512 * 1024 } });
    expect(networkRxSeries([sample])).toEqual([2]);
    expect(networkTxSeries([sample])).toEqual([0.5]);
  });

  it('picks a chart max with headroom above the peak, floored at 1', () => {
    expect(networkChartMax([0, 0.1], [0, 0.05])).toBe(1.2); // peak floored to 1, +20% headroom
    expect(networkChartMax([5], [2])).toBe(6);
  });

  it('sums rx+tx for the tile header reading', () => {
    const sample = makeSample({ network: { rxBytesPerSec: 100, txBytesPerSec: 50 } });
    expect(networkTotalBytesPerSec(sample)).toBe(150);
  });
});

describe('tile header "now" readings', () => {
  it('averages CPU core utilization for the latest sample', () => {
    const sample = makeSample({ cpu: { cores: [{ utilization: 10, frequencyMhz: 0 }, { utilization: 30, frequencyMhz: 0 }] } });
    expect(cpuUtilizationNow(sample)).toBe(20);
  });

  it('computes memory percent for the latest sample', () => {
    const sample = makeSample({ memory: { usedMb: 400, totalMb: 800, swapUsedMb: 0, swapTotalMb: 0 } });
    expect(memoryPercentNow(sample)).toBe(50);
  });
});

describe('Dashboard component', () => {
  it('renders a placeholder before the first sample arrives', () => {
    TestBed.configureTestingModule({
      imports: [Dashboard],
      providers: [
        {
          provide: MetricsService,
          useValue: {
            status: signal('connecting'),
            latest: signal(null),
            systemInfo: signal(null),
            history: signal([]),
          },
        },
      ],
    });
    const fixture = TestBed.createComponent(Dashboard);
    fixture.detectChanges();
    const text = (fixture.nativeElement as HTMLElement).textContent ?? '';
    expect(text).toContain('Waiting for the first reading');
  });

  it('renders GPU cards once a sample arrives', () => {
    TestBed.configureTestingModule({
      imports: [Dashboard],
      providers: [
        {
          provide: MetricsService,
          useValue: {
            status: signal('open'),
            latest: signal(makeSample()),
            systemInfo: signal({ hostname: 'h', kernel: 'k', provider: 'proc', cpuCount: 2 }),
            history: signal([makeSample()]),
          },
        },
      ],
    });
    const fixture = TestBed.createComponent(Dashboard);
    fixture.detectChanges();
    const text = (fixture.nativeElement as HTMLElement).textContent ?? '';
    expect(text).toContain('GPU0');
  });

  it('renders the process table from the latest sample', () => {
    TestBed.configureTestingModule({
      imports: [Dashboard],
      providers: [
        {
          provide: MetricsService,
          useValue: {
            status: signal('open'),
            latest: signal(makeSample()),
            systemInfo: signal({ hostname: 'h', kernel: 'k', provider: 'proc', cpuCount: 2 }),
            history: signal([makeSample()]),
          },
        },
      ],
    });
    const fixture = TestBed.createComponent(Dashboard);
    fixture.detectChanges();
    const text = (fixture.nativeElement as HTMLElement).textContent ?? '';
    expect(text).toContain('init');
    expect(text).toContain('Processes');
  });

  function create(sample: Sample) {
    TestBed.configureTestingModule({
      imports: [Dashboard],
      providers: [
        {
          provide: MetricsService,
          useValue: {
            status: signal('open'),
            latest: signal(sample),
            systemInfo: signal(null),
            history: signal([sample]),
          },
        },
      ],
    });
    const fixture = TestBed.createComponent(Dashboard);
    fixture.detectChanges();
    return fixture;
  }

  const processCard = (root: HTMLElement) => root.querySelector<HTMLElement>('.processes')!;
  const cells = (card: HTMLElement, field: string) =>
    Array.from(card.querySelectorAll<HTMLElement>(`tr.grid-row td[data-field="${field}"]`)).map((td) => td.textContent!.trim());

  const busy = makeSample({
    processes: [
      { pid: 2, name: 'bravo', cpu: 5, memoryMb: 2048 },
      { pid: 1, name: 'alpha', cpu: 30.456, memoryMb: 50 },
      { pid: 3, name: 'charlie', cpu: 15, memoryMb: 800 },
    ],
  });

  it('shows the processes busiest first, with formatted CPU and memory', async () => {
    const fixture = create(busy);
    await fixture.whenStable();
    const card = processCard(fixture.nativeElement);
    expect(cells(card, 'name')).toEqual(['alpha', 'charlie', 'bravo']);
    expect(cells(card, 'cpu')).toEqual(['30.46', '15', '5']);
    expect(cells(card, 'memoryMb')).toEqual(['50 MiB', '800 MiB', '2.00 GiB']);
  });

  it('sorts the processes by the header the user clicks', async () => {
    const fixture = create(busy);
    await fixture.whenStable();
    const card = processCard(fixture.nativeElement);
    card.querySelector<HTMLElement>('th[data-field="name"]')!.click();
    await fixture.whenStable();
    expect(cells(card, 'name')).toEqual(['alpha', 'bravo', 'charlie']);
    card.querySelector<HTMLElement>('th[data-field="memoryMb"]')!.click();
    await fixture.whenStable();
    expect(cells(card, 'pid')).toEqual(['1', '3', '2']);
  });

  it('says so when there are no processes', async () => {
    const fixture = create(makeSample({ processes: [] }));
    await fixture.whenStable();
    expect(processCard(fixture.nativeElement).textContent).toContain('No process data available.');
  });
});
